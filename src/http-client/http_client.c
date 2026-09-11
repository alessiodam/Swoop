#include "http-client/http_internal.h"

#include <string.h>

enum {
    CHUNK_SIZE = 0,
    CHUNK_DATA,
    CHUNK_CRLF,
    CHUNK_TRAILER
};

static const char *const method_names[HTTP_METHOD_COUNT] = {
    "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"
};

static const char *const state_names[] = {
    "idle", "connecting", "sending head", "sending body", "status",
    "headers", "body", "done", "failed"
};

static const char *const error_names[HTTP_ERROR_COUNT] = {
    "ok",
    "bad url",
    "client busy",
    "bridge link down",
    "wifi offline",
    "no socket",
    "connect failed",
    "send failed",
    "timed out",
    "connection closed",
    "bad response",
    "request too large",
    "redirect failed",
    "body rejected",
    "aborted"
};

static void client_start(http_client_t *client);

static void touch(http_client_t *client)
{
    client->deadline = witi_ms() + client->idle_timeout;
}

static void socket_drop(http_client_t *client)
{
    witi_socket_t *socket = client->socket;

    client->socket = NULL;
    client->socket_connected = false;
    client->held = false;

    if (socket != NULL)
    {
        witi_close(socket);
    }
}

static void sink_write(http_client_t *client, const uint8_t *data, size_t length)
{
    size_t room;

    if (client->sink == NULL || client->sink_capacity == 0)
    {
        return;
    }

    room = client->sink_capacity - 1 - client->sink_length;

    if (length > room)
    {
        length = room;
        client->sink_overflow = true;
    }

    if (length > 0)
    {
        memcpy(&client->sink[client->sink_length], data, length);
        client->sink_length += length;
    }

    client->sink[client->sink_length] = 0;
}

static void emit(http_client_t *client, const uint8_t *data, size_t length)
{
    client->received += length;

    touch(client);

    if (client->discarding || length == 0)
    {
        return;
    }

    sink_write(client, data, length);

    if (client->callbacks.on_body != NULL &&
        !client->callbacks.on_body(client, data, length, client->callbacks.user))
    {
        client->abort = true;
    }
}

static size_t take_line(http_client_t *client, const uint8_t *data, size_t length,
                        bool *ready)
{
    size_t index = 0;

    *ready = false;

    while (index < length)
    {
        char c = (char)data[index];

        index++;

        if (c == '\n')
        {
            while (client->line_length > 0 && client->line[client->line_length - 1] == '\r')
            {
                client->line_length--;
            }

            client->line[client->line_length] = 0;
            *ready = true;

            return index;
        }

        if (client->line_length + 1 < HTTP_LINE_MAX)
        {
            client->line[client->line_length] = c;
            client->line_length++;
        }
        else
        {
            client->line_overflow = true;
        }
    }

    return index;
}

static void line_reset(http_client_t *client)
{
    client->line_length = 0;
    client->line[0] = 0;
    client->line_overflow = false;
}

static void response_reset(http_client_t *client)
{
    line_reset(client);
    http_headers_clear(&client->response);

    client->status = 0;
    client->reason[0] = 0;
    client->body_mode = HTTP_BODY_NONE;
    client->chunk_state = CHUNK_SIZE;
    client->content_length = HTTP_LENGTH_UNKNOWN;
    client->chunk_left = 0;
    client->received = 0;
    client->response_close = false;
    client->discarding = false;
    client->redirecting = false;
    client->location[0] = 0;
}

static void raise_error(http_client_t *client, http_error_t error)
{
    client->error = error;
    client->abort = true;
}

static bool parse_status(http_client_t *client)
{
    const char *cursor = client->line;
    bool valid = false;
    uint32_t code;

    if (!http_iprefix(cursor, "HTTP/"))
    {
        return false;
    }

    while (*cursor != 0 && *cursor != ' ')
    {
        cursor++;
    }

    cursor = http_skip_space(cursor);
    code = http_parse_number(cursor, &valid);

    if (!valid || code < 100u || code > 599u)
    {
        return false;
    }

    client->status = (uint16_t)code;

    while (*cursor >= '0' && *cursor <= '9')
    {
        cursor++;
    }

    cursor = http_skip_space(cursor);
    http_copy(client->reason, sizeof client->reason, cursor);

    if (client->reason[0] == 0)
    {
        http_copy(client->reason, sizeof client->reason, http_status_text(client->status));
    }

    return true;
}

static void store_header(http_client_t *client)
{
    char *split = strchr(client->line, ':');
    const char *value;

    if (split == NULL)
    {
        return;
    }

    *split = 0;
    value = http_skip_space(split + 1);

    http_trim(client->line);
    http_headers_add(&client->response, client->line, value);
}

static void report_headers(http_client_t *client)
{
    uint8_t index;

    if (client->callbacks.on_status != NULL)
    {
        client->callbacks.on_status(client, client->status, client->reason,
                                    client->callbacks.user);
    }

    if (client->callbacks.on_header != NULL)
    {
        for (index = 0; index < client->response.count; index++)
        {
            client->callbacks.on_header(client,
                                        http_headers_name_at(&client->response, index),
                                        http_headers_value_at(&client->response, index),
                                        client->callbacks.user);
        }
    }

    if (client->callbacks.on_response != NULL)
    {
        client->callbacks.on_response(client, client->callbacks.user);
    }
}

static void headers_complete(http_client_t *client)
{
    const char *value;
    bool head_request = client->request.method == HTTP_HEAD;

    if (client->status < 200u)
    {
        response_reset(client);
        client->state = HTTP_STATE_STATUS;
        return;
    }

    if (http_headers_truncated(&client->response))
    {
        raise_error(client, HTTP_ERR_HEAD_TOO_LARGE);
        return;
    }

    value = http_headers_get(&client->response, "Connection");

    if (http_token_present(value, "close"))
    {
        client->response_close = true;
    }

    value = http_headers_get(&client->response, "Transfer-Encoding");

    if (head_request || client->status == 204u || client->status == 304u)
    {
        client->body_mode = HTTP_BODY_NONE;
    }
    else if (http_token_present(value, "chunked"))
    {
        client->body_mode = HTTP_BODY_CHUNKED;
        client->chunk_state = CHUNK_SIZE;
    }
    else
    {
        bool valid = false;
        uint32_t length = http_parse_number(
            http_headers_get(&client->response, "Content-Length"), &valid);

        if (valid)
        {
            client->content_length = length;
            client->body_mode = length > 0 ? HTTP_BODY_LENGTH : HTTP_BODY_NONE;
        }
        else
        {
            client->body_mode = HTTP_BODY_UNTIL_CLOSE;
            client->response_close = true;
        }
    }

    if (client->request.follow_redirects && http_status_is_redirect(client->status) &&
        client->redirects < client->request.max_redirects)
    {
        const char *location = http_headers_get(&client->response, "Location");

        if (location != NULL && location[0] != 0)
        {
            bool follow = true;

            if (client->callbacks.on_redirect != NULL)
            {
                follow = client->callbacks.on_redirect(client, location,
                                                       client->callbacks.user);
            }

            if (follow)
            {
                http_copy(client->location, sizeof client->location, location);
                client->redirecting = true;
                client->discarding = true;
            }
        }
    }

    if (!client->redirecting)
    {
        report_headers(client);
    }

    line_reset(client);

    if (client->body_mode == HTTP_BODY_NONE)
    {
        client->state = HTTP_STATE_DONE;
    }
    else
    {
        client->state = HTTP_STATE_BODY;
    }
}

static size_t feed_body(http_client_t *client, const uint8_t *data, size_t length)
{
    size_t index = 0;

    switch (client->body_mode)
    {
        case HTTP_BODY_LENGTH:
        {
            uint32_t left = client->content_length - client->received;
            size_t take = length;

            if ((uint32_t)take > left)
            {
                take = (size_t)left;
            }

            emit(client, data, take);
            index = take;

            if (client->received >= client->content_length)
            {
                client->state = HTTP_STATE_DONE;
            }

            break;
        }

        case HTTP_BODY_UNTIL_CLOSE:
            emit(client, data, length);
            index = length;
            break;

        case HTTP_BODY_CHUNKED:
            while (index < length && client->state == HTTP_STATE_BODY && !client->abort)
            {
                if (client->chunk_state == CHUNK_DATA)
                {
                    size_t take = length - index;

                    if ((uint32_t)take > client->chunk_left)
                    {
                        take = (size_t)client->chunk_left;
                    }

                    emit(client, &data[index], take);
                    index += take;
                    client->chunk_left -= take;

                    if (client->chunk_left == 0)
                    {
                        client->chunk_state = CHUNK_CRLF;
                        line_reset(client);
                    }
                }
                else
                {
                    bool ready = false;

                    index += take_line(client, &data[index], length - index, &ready);

                    if (!ready)
                    {
                        break;
                    }

                    if (client->chunk_state == CHUNK_SIZE)
                    {
                        bool valid = false;
                        uint32_t size;

                        if (client->line[0] == 0)
                        {
                            line_reset(client);
                            continue;
                        }

                        size = http_parse_hex(client->line, &valid);

                        if (!valid)
                        {
                            raise_error(client, HTTP_ERR_PROTOCOL);
                            return index;
                        }

                        line_reset(client);

                        if (size == 0)
                        {
                            client->chunk_state = CHUNK_TRAILER;
                        }
                        else
                        {
                            client->chunk_left = size;
                            client->chunk_state = CHUNK_DATA;
                        }
                    }
                    else if (client->chunk_state == CHUNK_CRLF)
                    {
                        line_reset(client);
                        client->chunk_state = CHUNK_SIZE;
                    }
                    else
                    {
                        bool done = client->line[0] == 0;

                        line_reset(client);

                        if (done)
                        {
                            client->state = HTTP_STATE_DONE;
                        }
                    }
                }
            }

            break;

        default:
            index = length;
            break;
    }

    return index;
}

static void feed(http_client_t *client, const uint8_t *data, size_t length)
{
    if (client->state == HTTP_STATE_SENDING_HEAD ||
        client->state == HTTP_STATE_SENDING_BODY)
    {
        client->head_sent = client->head_length;
        client->body_left = 0;
        client->stage_length = 0;
        client->stage_sent = 0;
        client->source_done = true;
        client->state = HTTP_STATE_STATUS;
    }

    while (length > 0 && !client->abort)
    {
        size_t used = 0;

        if (client->state == HTTP_STATE_STATUS)
        {
            bool ready = false;

            used = take_line(client, data, length, &ready);

            if (ready)
            {
                if (client->line[0] == 0)
                {
                    line_reset(client);
                }
                else if (!parse_status(client))
                {
                    raise_error(client, HTTP_ERR_PROTOCOL);
                    return;
                }
                else
                {
                    line_reset(client);
                    client->state = HTTP_STATE_HEADERS;
                }
            }
        }
        else if (client->state == HTTP_STATE_HEADERS)
        {
            bool ready = false;

            used = take_line(client, data, length, &ready);

            if (ready)
            {
                if (client->line[0] == 0)
                {
                    headers_complete(client);
                }
                else
                {
                    store_header(client);
                    line_reset(client);
                }
            }
        }
        else if (client->state == HTTP_STATE_BODY)
        {
            used = feed_body(client, data, length);
        }
        else
        {
            return;
        }

        if (used == 0)
        {
            return;
        }

        data += used;
        length -= used;

        touch(client);
    }
}

static void on_socket_connect(witi_socket_t *socket, void *user)
{
    http_client_t *client = user;

    (void)socket;

    client->socket_connected = true;
    touch(client);
}

static void on_socket_data(witi_socket_t *socket, const void *data, size_t length,
                           void *user)
{
    http_client_t *client = user;

    (void)socket;

    if (client->active)
    {
        feed(client, data, length);
    }
}

static void on_socket_eof(witi_socket_t *socket, void *user)
{
    http_client_t *client = user;

    (void)socket;

    client->socket_eof = true;
}

static void on_socket_error(witi_socket_t *socket, uint8_t code, void *user)
{
    http_client_t *client = user;

    (void)socket;

    client->socket_failed = true;
    client->socket_error = code;
}

static void on_socket_close(witi_socket_t *socket, void *user)
{
    http_client_t *client = user;

    if (client->socket == socket)
    {
        client->socket = NULL;
        client->socket_connected = false;
        client->socket_eof = true;
        client->held = false;
    }
}

static bool head_add(http_client_t *client, const char *text)
{
    size_t need;

    if (text == NULL)
    {
        return true;
    }

    need = strlen(text);

    if (client->head_length + need + 1 > HTTP_HEAD_MAX)
    {
        return false;
    }

    memcpy(&client->head[client->head_length], text, need);
    client->head_length += need;
    client->head[client->head_length] = 0;

    return true;
}

static bool head_field(http_client_t *client, const char *name, const char *value)
{
    return head_add(client, name) && head_add(client, ": ") &&
           head_add(client, value) && head_add(client, "\r\n");
}

static bool user_has(const http_client_t *client, const char *name)
{
    return client->request.headers != NULL &&
           http_headers_has(client->request.headers, name);
}

static bool build_head(http_client_t *client)
{
    const http_request_t *request = &client->request;
    char number[12];
    uint8_t index;
    bool has_body = request->body != NULL || request->body_source != NULL;

    client->head_length = 0;
    client->head[0] = 0;
    client->head_sent = 0;
    client->request_chunked = false;

    if (!head_add(client, request->method_name != NULL
                              ? request->method_name
                              : http_method_name(request->method)))
    {
        return false;
    }

    if (!head_add(client, " ") || !head_add(client, client->url.path) ||
        !head_add(client, " " HTTP_VERSION "\r\n"))
    {
        return false;
    }

    if (!head_add(client, "Host: ") || !head_add(client, client->url.host))
    {
        return false;
    }

    if ((client->url.tls && client->url.port != 443) ||
        (!client->url.tls && client->url.port != 80))
    {
        http_format_number(number, sizeof number, client->url.port);

        if (!head_add(client, ":") || !head_add(client, number))
        {
            return false;
        }
    }

    if (!head_add(client, "\r\n"))
    {
        return false;
    }

    if (!user_has(client, "User-Agent") &&
        !head_field(client, "User-Agent",
                    request->user_agent != NULL ? request->user_agent : HTTP_USER_AGENT))
    {
        return false;
    }

    if (!user_has(client, "Accept") &&
        !head_field(client, "Accept", request->accept != NULL ? request->accept : "*/*"))
    {
        return false;
    }

    if (!user_has(client, "Connection") &&
        !head_field(client, "Connection", request->keep_alive ? "keep-alive" : "close"))
    {
        return false;
    }

    if (request->content_type != NULL && !user_has(client, "Content-Type") &&
        !head_field(client, "Content-Type", request->content_type))
    {
        return false;
    }

    if (!user_has(client, "Content-Length") && !user_has(client, "Transfer-Encoding"))
    {
        if (request->body != NULL)
        {
            http_format_number(number, sizeof number, (uint32_t)request->body_length);

            if (!head_field(client, "Content-Length", number))
            {
                return false;
            }
        }
        else if (request->body_source != NULL)
        {
            if (request->body_total == HTTP_LENGTH_UNKNOWN)
            {
                client->request_chunked = true;

                if (!head_field(client, "Transfer-Encoding", "chunked"))
                {
                    return false;
                }
            }
            else
            {
                http_format_number(number, sizeof number, request->body_total);

                if (!head_field(client, "Content-Length", number))
                {
                    return false;
                }
            }
        }
        else if (request->method == HTTP_POST || request->method == HTTP_PUT ||
                 request->method == HTTP_PATCH)
        {
            if (!head_field(client, "Content-Length", "0"))
            {
                return false;
            }
        }
    }

    for (index = 0; index < http_headers_count(request->headers); index++)
    {
        if (!head_field(client, http_headers_name_at(request->headers, index),
                        http_headers_value_at(request->headers, index)))
        {
            return false;
        }
    }

    if (!head_add(client, "\r\n"))
    {
        return false;
    }

    if (request->body != NULL)
    {
        client->body_cursor = request->body;
        client->body_left = request->body_length;
    }
    else
    {
        client->body_cursor = NULL;
        client->body_left = 0;
    }

    client->stage_length = 0;
    client->stage_sent = 0;
    client->source_done = !has_body || request->body_source == NULL;

    return true;
}

static void client_finish(http_client_t *client)
{
    bool reusable = client->request.keep_alive && !client->response_close &&
                    client->socket != NULL && !client->socket_eof &&
                    !client->socket_failed && client->body_mode != HTTP_BODY_UNTIL_CLOSE;

    client->elapsed = witi_ms() - client->started;

    if (reusable)
    {
        client->held = true;
        client->held_tls = client->url.tls;
        client->held_port = client->url.port;
        http_copy(client->held_host, sizeof client->held_host, client->url.host);
    }
    else
    {
        socket_drop(client);
    }

    client->active = false;
    client->finished = true;
    client->error = HTTP_OK;
    client->state = HTTP_STATE_DONE;

    if (client->callbacks.on_done != NULL)
    {
        client->callbacks.on_done(client, HTTP_OK, client->callbacks.user);
    }
}

static void client_fail(http_client_t *client, http_error_t error)
{
    client->elapsed = witi_ms() - client->started;

    socket_drop(client);

    client->active = false;
    client->finished = true;
    client->error = error;
    client->state = HTTP_STATE_FAILED;

    if (client->callbacks.on_done != NULL)
    {
        client->callbacks.on_done(client, error, client->callbacks.user);
    }
}

static void client_redirect(http_client_t *client)
{
    uint16_t status = client->status;

    if (!http_url_resolve(&client->url, client->location))
    {
        client_fail(client, HTTP_ERR_REDIRECT);
        return;
    }

    client->redirects++;

    if (status == 303u ||
        ((status == 301u || status == 302u) && client->request.method != HTTP_GET &&
         client->request.method != HTTP_HEAD))
    {
        client->request.method = HTTP_GET;
        client->request.method_name = NULL;
        client->request.body = NULL;
        client->request.body_length = 0;
        client->request.body_source = NULL;
        client->request.body_total = HTTP_LENGTH_UNKNOWN;
        client->request.content_type = NULL;
    }
    else if (client->request.body_source != NULL)
    {
        client_fail(client, HTTP_ERR_REDIRECT);
        return;
    }

    if (client->socket != NULL)
    {
        bool same = client->request.keep_alive && !client->response_close &&
                    !client->socket_eof && !client->socket_failed &&
                    client->body_mode != HTTP_BODY_UNTIL_CLOSE &&
                    client->held_tls == client->url.tls &&
                    client->held_port == client->url.port &&
                    http_iequal(client->held_host, client->url.host);

        if (same)
        {
            client->held = true;
        }
        else
        {
            socket_drop(client);
        }
    }

    client->sink_length = 0;
    client->sink_overflow = false;

    if (client->sink != NULL && client->sink_capacity > 0)
    {
        client->sink[0] = 0;
    }

    client_start(client);
}

static void client_start(http_client_t *client)
{
    witi_handlers_t handlers;

    response_reset(client);

    client->abort = false;
    client->reused = false;
    client->socket_eof = false;
    client->socket_failed = false;
    client->socket_error = 0;

    if (!build_head(client))
    {
        client_fail(client, HTTP_ERR_HEAD_TOO_LARGE);
        return;
    }

    if (client->socket != NULL && client->held && client->held_tls == client->url.tls &&
        client->held_port == client->url.port &&
        http_iequal(client->held_host, client->url.host))
    {
        client->reused = true;
        client->held = false;
        client->socket_connected = true;
        client->state = HTTP_STATE_SENDING_HEAD;
        touch(client);
        return;
    }

    socket_drop(client);

    if (!witi_ready())
    {
        client_fail(client, HTTP_ERR_LINK);
        return;
    }

    if (!witi_wifi_online())
    {
        client_fail(client, HTTP_ERR_OFFLINE);
        return;
    }

    memset(&handlers, 0, sizeof handlers);

    handlers.on_connect = on_socket_connect;
    handlers.on_data = on_socket_data;
    handlers.on_eof = on_socket_eof;
    handlers.on_error = on_socket_error;
    handlers.on_close = on_socket_close;
    handlers.user = client;

    client->socket_connected = false;
    client->socket = witi_connect(client->url.tls ? WITI_TLS : WITI_TCP, client->url.host,
                                  client->url.port, &handlers);

    if (client->socket == NULL)
    {
        client_fail(client, HTTP_ERR_SOCKET);
        return;
    }

    client->held_tls = client->url.tls;
    client->held_port = client->url.port;
    http_copy(client->held_host, sizeof client->held_host, client->url.host);

    client->state = HTTP_STATE_CONNECTING;
    client->deadline = witi_ms() + client->request.connect_timeout_ms;
}

static bool client_retry(http_client_t *client)
{
    if (!client->reused || client->retried || client->status != 0)
    {
        return false;
    }

    client->retried = true;
    client->held = false;

    socket_drop(client);
    client_start(client);

    return true;
}

static void push_head(http_client_t *client)
{
    while (client->head_sent < client->head_length)
    {
        int sent = witi_send(client->socket, &client->head[client->head_sent],
                             client->head_length - client->head_sent);

        if (sent < 0)
        {
            client_fail(client, HTTP_ERR_SEND);
            return;
        }

        if (sent == 0)
        {
            return;
        }

        client->head_sent += (size_t)sent;
        touch(client);
    }

    if (client->body_left > 0 || !client->source_done)
    {
        client->state = HTTP_STATE_SENDING_BODY;
    }
    else
    {
        client->state = HTTP_STATE_STATUS;
    }
}

static bool stage_fill(http_client_t *client)
{
    char header[8];
    size_t limit = HTTP_SEND_CHUNK - 12;
    size_t got;
    size_t length;
    uint32_t value;

    if (client->stage_sent < client->stage_length)
    {
        return true;
    }

    client->stage_length = 0;
    client->stage_sent = 0;

    if (client->source_done)
    {
        return false;
    }

    got = client->request.body_source(&client->stage[8], limit,
                                      client->request.body_user);

    if (got > limit)
    {
        got = limit;
    }

    if (!client->request_chunked)
    {
        if (got == 0)
        {
            client->source_done = true;
            return false;
        }

        memmove(client->stage, &client->stage[8], got);
        client->stage_length = got;

        return true;
    }

    if (got == 0)
    {
        client->source_done = true;
        memcpy(client->stage, "0\r\n\r\n", 5);
        client->stage_length = 5;

        return true;
    }

    length = 0;
    value = (uint32_t)got;

    if (value >= 0x100u)
    {
        header[length] = (char)("0123456789abcdef"[(value >> 8) & 0x0F]);
        length++;
    }

    if (value >= 0x10u)
    {
        header[length] = (char)("0123456789abcdef"[(value >> 4) & 0x0F]);
        length++;
    }

    header[length] = (char)("0123456789abcdef"[value & 0x0F]);
    length++;
    header[length] = '\r';
    length++;
    header[length] = '\n';
    length++;

    memmove(&client->stage[length], &client->stage[8], got);
    memcpy(client->stage, header, length);

    client->stage[length + got] = '\r';
    client->stage[length + got + 1] = '\n';
    client->stage_length = length + got + 2;

    return true;
}

static void push_body(http_client_t *client)
{
    while (client->body_left > 0)
    {
        int sent = witi_send(client->socket, client->body_cursor, client->body_left);

        if (sent < 0)
        {
            client_fail(client, HTTP_ERR_SEND);
            return;
        }

        if (sent == 0)
        {
            return;
        }

        client->body_cursor += (size_t)sent;
        client->body_left -= (size_t)sent;
        touch(client);
    }

    if (client->request.body_source != NULL)
    {
        while (stage_fill(client))
        {
            while (client->stage_sent < client->stage_length)
            {
                int sent = witi_send(client->socket, &client->stage[client->stage_sent],
                                     client->stage_length - client->stage_sent);

                if (sent < 0)
                {
                    client_fail(client, HTTP_ERR_SEND);
                    return;
                }

                if (sent == 0)
                {
                    return;
                }

                client->stage_sent += (size_t)sent;
                touch(client);
            }
        }

        if (!client->source_done)
        {
            return;
        }
    }

    client->state = HTTP_STATE_STATUS;
}

void http_init(http_client_t *client)
{
    if (client == NULL)
    {
        return;
    }

    memset(client, 0, sizeof *client);

    client->state = HTTP_STATE_IDLE;
    client->content_length = HTTP_LENGTH_UNKNOWN;
    client->idle_timeout = HTTP_IDLE_TIMEOUT_DEFAULT;
}

void http_request_init(http_request_t *request, http_method_t method)
{
    if (request == NULL)
    {
        return;
    }

    memset(request, 0, sizeof *request);

    request->method = method;
    request->body_total = HTTP_LENGTH_UNKNOWN;
    request->follow_redirects = true;
    request->max_redirects = HTTP_REDIRECTS_DEFAULT;
    request->keep_alive = true;
    request->connect_timeout_ms = HTTP_CONNECT_TIMEOUT_DEFAULT;
    request->idle_timeout_ms = HTTP_IDLE_TIMEOUT_DEFAULT;
}

bool http_begin(http_client_t *client, const char *url, const http_request_t *request,
                const http_callbacks_t *callbacks)
{
    if (client == NULL || url == NULL)
    {
        return false;
    }

    if (client->active)
    {
        client->error = HTTP_ERR_BUSY;
        return false;
    }

    if (request != NULL)
    {
        client->request = *request;
    }
    else
    {
        http_request_init(&client->request, HTTP_GET);
    }

    if (client->request.connect_timeout_ms == 0)
    {
        client->request.connect_timeout_ms = HTTP_CONNECT_TIMEOUT_DEFAULT;
    }

    if (client->request.idle_timeout_ms == 0)
    {
        client->request.idle_timeout_ms = HTTP_IDLE_TIMEOUT_DEFAULT;
    }

    if (client->request.follow_redirects && client->request.max_redirects == 0)
    {
        client->request.max_redirects = HTTP_REDIRECTS_DEFAULT;
    }

    if (callbacks != NULL)
    {
        client->callbacks = *callbacks;
    }
    else
    {
        memset(&client->callbacks, 0, sizeof client->callbacks);
    }

    if (!http_url_parse(&client->url, url))
    {
        client->error = HTTP_ERR_URL;
        client->state = HTTP_STATE_FAILED;
        client->finished = true;
        return false;
    }

    client->idle_timeout = client->request.idle_timeout_ms;
    client->error = HTTP_OK;
    client->finished = false;
    client->abort = false;
    client->retried = false;
    client->redirects = 0;
    client->sink_length = 0;
    client->sink_overflow = false;
    client->elapsed = 0;
    client->started = witi_ms();
    client->active = true;

    if (client->sink != NULL && client->sink_capacity > 0)
    {
        client->sink[0] = 0;
    }

    client_start(client);

    return client->active || client->error == HTTP_OK;
}

void http_update(http_client_t *client)
{
    if (client == NULL || !client->active)
    {
        return;
    }

    if (client->abort)
    {
        client_fail(client, client->error != HTTP_OK ? client->error : HTTP_ERR_ABORTED);
        return;
    }

    if (client->state == HTTP_STATE_DONE)
    {
        if (client->redirecting)
        {
            client_redirect(client);
        }
        else
        {
            client_finish(client);
        }

        return;
    }

    if (client->state == HTTP_STATE_CONNECTING)
    {
        if (client->socket == NULL || client->socket_failed)
        {
            client_fail(client, HTTP_ERR_CONNECT);
            return;
        }

        if (client->socket_connected)
        {
            client->state = HTTP_STATE_SENDING_HEAD;
            touch(client);
        }
        else if ((int32_t)(witi_ms() - client->deadline) >= 0)
        {
            client_fail(client, HTTP_ERR_TIMEOUT);
            return;
        }
        else
        {
            return;
        }
    }

    if (client->socket == NULL || client->socket_failed)
    {
        if (client->state == HTTP_STATE_BODY &&
            client->body_mode == HTTP_BODY_UNTIL_CLOSE && !client->socket_failed)
        {
            client->state = HTTP_STATE_DONE;
            http_update(client);
            return;
        }

        if (client_retry(client))
        {
            return;
        }

        client_fail(client, client->socket_failed ? HTTP_ERR_SOCKET : HTTP_ERR_CLOSED);
        return;
    }

    if (client->state == HTTP_STATE_SENDING_HEAD)
    {
        push_head(client);
    }

    if (client->state == HTTP_STATE_SENDING_BODY)
    {
        push_body(client);
    }

    if (!client->active)
    {
        return;
    }

    if (client->state == HTTP_STATE_DONE)
    {
        http_update(client);
        return;
    }

    if (client->socket_eof && witi_available(client->socket) == 0)
    {
        if (client->state == HTTP_STATE_BODY &&
            client->body_mode == HTTP_BODY_UNTIL_CLOSE)
        {
            client->state = HTTP_STATE_DONE;
            http_update(client);
            return;
        }

        if (client_retry(client))
        {
            return;
        }

        client_fail(client, HTTP_ERR_CLOSED);
        return;
    }

    if ((int32_t)(witi_ms() - client->deadline) >= 0)
    {
        client_fail(client, HTTP_ERR_TIMEOUT);
    }
}

bool http_busy(const http_client_t *client)
{
    return client != NULL && client->active;
}

bool http_finished(const http_client_t *client)
{
    return client != NULL && client->finished;
}

void http_cancel(http_client_t *client)
{
    if (client == NULL || !client->active)
    {
        return;
    }

    client_fail(client, HTTP_ERR_ABORTED);
}

void http_release(http_client_t *client)
{
    if (client == NULL)
    {
        return;
    }

    socket_drop(client);
}

void http_set_sink(http_client_t *client, void *buffer, size_t capacity)
{
    if (client == NULL)
    {
        return;
    }

    client->sink = buffer;
    client->sink_capacity = buffer != NULL ? capacity : 0;
    client->sink_length = 0;
    client->sink_overflow = false;

    if (client->sink != NULL && client->sink_capacity > 0)
    {
        client->sink[0] = 0;
    }
}

const char *http_text(const http_client_t *client)
{
    if (client == NULL || client->sink == NULL)
    {
        return "";
    }

    return (const char *)client->sink;
}

const void *http_data(const http_client_t *client)
{
    return client != NULL ? client->sink : NULL;
}

size_t http_length(const http_client_t *client)
{
    return client != NULL ? client->sink_length : 0;
}

bool http_truncated(const http_client_t *client)
{
    return client != NULL && client->sink_overflow;
}

http_error_t http_perform(http_client_t *client, const char *url,
                          const http_request_t *request, const http_callbacks_t *callbacks)
{
    if (client == NULL)
    {
        return HTTP_ERR_URL;
    }

    if (!http_begin(client, url, request, callbacks))
    {
        return client->error;
    }

    while (http_busy(client))
    {
        witi_poll();
        http_update(client);

        if (client->callbacks.on_idle != NULL && http_busy(client) &&
            !client->callbacks.on_idle(client, client->callbacks.user))
        {
            http_cancel(client);
        }
    }

    return client->error;
}

http_error_t http_get(http_client_t *client, const char *url, void *buffer, size_t capacity)
{
    http_request_t request;

    http_request_init(&request, HTTP_GET);
    http_set_sink(client, buffer, capacity);

    return http_perform(client, url, &request, NULL);
}

http_error_t http_post(http_client_t *client, const char *url, const char *content_type,
                       const void *body, size_t length, void *buffer, size_t capacity)
{
    http_request_t request;

    http_request_init(&request, HTTP_POST);

    request.content_type = content_type;
    request.body = body;
    request.body_length = length;

    http_set_sink(client, buffer, capacity);

    return http_perform(client, url, &request, NULL);
}

http_error_t http_put(http_client_t *client, const char *url, const char *content_type,
                      const void *body, size_t length, void *buffer, size_t capacity)
{
    http_request_t request;

    http_request_init(&request, HTTP_PUT);

    request.content_type = content_type;
    request.body = body;
    request.body_length = length;

    http_set_sink(client, buffer, capacity);

    return http_perform(client, url, &request, NULL);
}

http_error_t http_delete(http_client_t *client, const char *url, void *buffer,
                         size_t capacity)
{
    http_request_t request;

    http_request_init(&request, HTTP_DELETE);
    http_set_sink(client, buffer, capacity);

    return http_perform(client, url, &request, NULL);
}

http_error_t http_head(http_client_t *client, const char *url)
{
    http_request_t request;

    http_request_init(&request, HTTP_HEAD);
    http_set_sink(client, NULL, 0);

    return http_perform(client, url, &request, NULL);
}

http_state_t http_state(const http_client_t *client)
{
    return client != NULL ? client->state : HTTP_STATE_IDLE;
}

const char *http_state_name(http_state_t state)
{
    if ((unsigned)state >= sizeof state_names / sizeof state_names[0])
    {
        return "unknown";
    }

    return state_names[state];
}

http_error_t http_error(const http_client_t *client)
{
    return client != NULL ? client->error : HTTP_ERR_URL;
}

const char *http_error_text(http_error_t error)
{
    if ((unsigned)error >= HTTP_ERROR_COUNT)
    {
        return "unknown error";
    }

    return error_names[error];
}

uint16_t http_status(const http_client_t *client)
{
    return client != NULL ? client->status : 0;
}

const char *http_reason(const http_client_t *client)
{
    return client != NULL ? client->reason : "";
}

uint32_t http_content_length(const http_client_t *client)
{
    return client != NULL ? client->content_length : HTTP_LENGTH_UNKNOWN;
}

uint32_t http_received(const http_client_t *client)
{
    return client != NULL ? client->received : 0;
}

uint32_t http_elapsed_ms(const http_client_t *client)
{
    if (client == NULL)
    {
        return 0;
    }

    return client->active ? witi_ms() - client->started : client->elapsed;
}

uint8_t http_redirect_count(const http_client_t *client)
{
    return client != NULL ? client->redirects : 0;
}

http_body_mode_t http_body_mode(const http_client_t *client)
{
    return client != NULL ? (http_body_mode_t)client->body_mode : HTTP_BODY_NONE;
}

const http_headers_t *http_response_headers(const http_client_t *client)
{
    return client != NULL ? &client->response : NULL;
}

const char *http_response_header(const http_client_t *client, const char *name)
{
    return client != NULL ? http_headers_get(&client->response, name) : NULL;
}

const http_url_t *http_url(const http_client_t *client)
{
    return client != NULL ? &client->url : NULL;
}

const char *http_effective_url(http_client_t *client)
{
    if (client == NULL)
    {
        return "";
    }

    http_url_format(&client->url, client->text, sizeof client->text);

    return client->text;
}

const char *http_method_name(http_method_t method)
{
    if ((unsigned)method >= HTTP_METHOD_COUNT)
    {
        return "GET";
    }

    return method_names[method];
}

bool http_status_is_redirect(uint16_t status)
{
    return status == 301u || status == 302u || status == 303u || status == 307u ||
           status == 308u;
}

const char *http_status_text(uint16_t status)
{
    switch (status)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 410: return "Gone";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: break;
    }

    if (status >= 500u)
    {
        return "Server Error";
    }

    if (status >= 400u)
    {
        return "Client Error";
    }

    if (status >= 300u)
    {
        return "Redirect";
    }

    if (status >= 200u)
    {
        return "Success";
    }

    return "Informational";
}
