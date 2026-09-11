#include "swoop.h"

#include <string.h>

static http_client_t client;
static http_headers_t extra;
static http_request_t request;
static net_done_t pending;
static net_chunk_t streaming;
static net_head_t heading;
static char sink[NET_MAX];
static char address[NET_URL_MAX];
static char failure[40];
static uint16_t last_status;

static void on_status(http_client_t *handle, uint16_t status, const char *reason, void *user)
{
    (void)handle;
    (void)reason;
    (void)user;

    last_status = status;
}

static void on_header(http_client_t *handle, const char *name, const char *value, void *user)
{
    (void)handle;
    (void)user;

    if (heading != NULL)
    {
        heading(name, value);
    }
}

static bool on_body(http_client_t *handle, const void *data, size_t length, void *user)
{
    (void)handle;
    (void)user;

    if (streaming == NULL)
    {
        return true;
    }
    return streaming(data, length);
}

static void on_done(http_client_t *handle, http_error_t error, void *user)
{
    net_done_t done = pending;
    uint16_t status = http_status(handle);

    (void)user;

    pending = NULL;
    streaming = NULL;
    heading = NULL;

    if (error != HTTP_OK && error != HTTP_ERR_ABORTED)
    {
        const char *text = handle->socket_error != 0
                               ? witi_error_name(handle->socket_error)
                               : http_error_text(error);

        strncpy(failure, text, sizeof failure - 1);
        failure[sizeof failure - 1] = 0;
        status = 0;
    }
    else
    {
        failure[0] = 0;
    }

    last_status = status;

    if (done != NULL)
    {
        size_t length = http_length(handle);

        if (length >= NET_MAX)
        {
            length = NET_MAX - 1;
        }

        sink[length] = 0;
        done(status, sink, length);
    }
}

static const http_callbacks_t callbacks = {
    on_status, on_header, NULL, on_body, NULL, NULL, on_done, NULL
};

void net_init(void)
{
    http_init(&client);
    http_headers_clear(&extra);

    pending = NULL;
    streaming = NULL;
    heading = NULL;
    failure[0] = 0;
    last_status = 0;
}

void net_update(void)
{
    http_update(&client);
}

void net_release(void)
{
    http_release(&client);
}

void net_cancel(void)
{
    pending = NULL;
    streaming = NULL;
    heading = NULL;

    if (http_busy(&client))
    {
        http_cancel(&client);
    }
}

bool net_busy(void)
{
    return http_busy(&client);
}

static bool begin(const char *url, const char *accept, net_chunk_t chunk, net_head_t head,
                  net_done_t done)
{
    if (http_busy(&client) || url == NULL || url[0] == 0)
    {
        return false;
    }

    http_headers_clear(&extra);
    http_headers_set(&extra, "Accept-Encoding", "identity");

    http_request_init(&request, HTTP_GET);

    request.accept = accept;
    request.user_agent = SWOOP_AGENT;
    request.headers = &extra;
    request.follow_redirects = false;
    request.keep_alive = true;
    request.idle_timeout_ms = 25000;

    pending = done;
    streaming = chunk;
    heading = head;
    failure[0] = 0;
    last_status = 0;
    sink[0] = 0;

    if (chunk != NULL)
    {
        http_set_sink(&client, NULL, 0);
    }
    else
    {
        http_set_sink(&client, sink, NET_MAX - 1);
    }

    if (!http_begin(&client, url, &request, &callbacks))
    {
        pending = NULL;
        streaming = NULL;
        heading = NULL;

        strncpy(failure, http_error_text(http_error(&client)), sizeof failure - 1);
        failure[sizeof failure - 1] = 0;

        return false;
    }

    return true;
}

bool net_get(const char *url, net_done_t done)
{
    return begin(url, "application/json", NULL, NULL, done);
}

bool net_stream(const char *url, net_chunk_t chunk, net_head_t head, net_done_t done)
{
    return begin(url, "application/octet-stream", chunk, head, done);
}

const char *net_error(void)
{
    return failure;
}

uint16_t net_status(void)
{
    return last_status;
}

uint32_t net_received(void)
{
    return http_received(&client);
}

uint32_t net_expected(void)
{
    return http_content_length(&client);
}

const char *net_stage(void)
{
    return http_state_name(http_state(&client));
}

char *net_url(const char *base, const char *path)
{
    size_t used;

    address[0] = 0;

    strncat(address, base, NET_URL_MAX - 1);

    used = strlen(address);

    while (used > 0 && address[used - 1] == '/')
    {
        used--;
        address[used] = 0;
    }

    strncat(address, path, NET_URL_MAX - used - 1);

    return address;
}

void net_escape(char *out, size_t max, const char *text)
{
    http_url_encode(out, max, text, "-._~");
}
