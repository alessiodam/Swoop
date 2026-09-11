#include "http-client/http_internal.h"

#include <string.h>

static bool url_unreserved(char c, const char *keep)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
    {
        return true;
    }

    if (c == '-' || c == '_' || c == '.' || c == '~')
    {
        return true;
    }

    return keep != NULL && strchr(keep, c) != NULL;
}

static char url_hex_digit(unsigned value)
{
    return (char)(value < 10 ? '0' + value : 'A' + (value - 10));
}

static int url_hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }

    return -1;
}

static void url_normalize_path(char *path)
{
    char output[HTTP_PATH_MAX];
    const char *cursor = path;
    const char *query = strchr(path, '?');
    size_t limit = query != NULL ? (size_t)(query - path) : strlen(path);
    size_t length = 0;
    size_t index = 0;

    output[0] = 0;

    if (limit == 0 || path[0] != '/')
    {
        output[0] = '/';
        length = 1;
    }

    while (index < limit)
    {
        size_t start;
        size_t span;

        while (index < limit && cursor[index] == '/')
        {
            index++;
        }

        start = index;

        while (index < limit && cursor[index] != '/')
        {
            index++;
        }

        span = index - start;

        if (span == 0)
        {
            continue;
        }

        if (span == 1 && cursor[start] == '.')
        {
            continue;
        }

        if (span == 2 && cursor[start] == '.' && cursor[start + 1] == '.')
        {
            while (length > 1)
            {
                length--;

                if (output[length] == '/')
                {
                    break;
                }
            }

            if (length == 0)
            {
                length = 1;
            }

            output[length] = 0;
            continue;
        }

        if (length == 0 || output[length - 1] != '/')
        {
            if (length + 1 < sizeof output)
            {
                output[length] = '/';
                length++;
            }
        }

        if (length + span < sizeof output)
        {
            memcpy(&output[length], &cursor[start], span);
            length += span;
        }
    }

    if (length == 0)
    {
        output[0] = '/';
        length = 1;
    }

    if (limit > 0 && path[limit - 1] == '/' && output[length - 1] != '/' &&
        length + 1 < sizeof output)
    {
        output[length] = '/';
        length++;
    }

    output[length] = 0;

    if (query != NULL)
    {
        size_t tail = strlen(query);

        if (length + tail + 1 < sizeof output)
        {
            memcpy(&output[length], query, tail + 1);
        }
    }

    http_copy(path, HTTP_PATH_MAX, output);
}

static bool url_split(http_url_t *url, const char *text)
{
    const char *authority;
    const char *cursor;
    const char *at;
    const char *path;
    size_t span;
    char host[HTTP_HOST_MAX];
    size_t index;

    text = http_skip_space(text);

    if (http_iprefix(text, "http://"))
    {
        url->tls = false;
        url->port = 80;
        authority = text + 7;
    }
    else if (http_iprefix(text, "https://"))
    {
        url->tls = true;
        url->port = 443;
        authority = text + 8;
    }
    else if (strstr(text, "://") != NULL)
    {
        return false;
    }
    else
    {
        url->tls = false;
        url->port = 80;
        authority = text;
    }

    cursor = authority;

    while (*cursor != 0 && *cursor != '/' && *cursor != '?' && *cursor != '#')
    {
        cursor++;
    }

    path = cursor;
    at = NULL;

    for (index = 0; authority + index < cursor; index++)
    {
        if (authority[index] == '@')
        {
            at = authority + index;
        }
    }

    if (at != NULL)
    {
        authority = at + 1;
    }

    span = (size_t)(cursor - authority);

    if (span == 0 || span >= sizeof host)
    {
        return false;
    }

    memcpy(host, authority, span);
    host[span] = 0;

    if (host[0] == '[')
    {
        char *end = strchr(host, ']');

        if (end == NULL)
        {
            return false;
        }

        if (end[1] == ':')
        {
            bool valid = false;
            uint32_t port = http_parse_number(end + 2, &valid);

            if (!valid || port == 0 || port > 65535u)
            {
                return false;
            }

            url->port = (uint16_t)port;
        }

        end[0] = 0;
        http_copy(url->host, sizeof url->host, host + 1);
    }
    else
    {
        char *colon = strrchr(host, ':');

        if (colon != NULL)
        {
            bool valid = false;
            uint32_t port = http_parse_number(colon + 1, &valid);

            if (!valid || port == 0 || port > 65535u)
            {
                return false;
            }

            url->port = (uint16_t)port;
            colon[0] = 0;
        }

        http_copy(url->host, sizeof url->host, host);
    }

    if (url->host[0] == 0)
    {
        return false;
    }

    for (index = 0; url->host[index] != 0; index++)
    {
        url->host[index] = (char)http_lower((unsigned char)url->host[index]);
    }

    if (*path == 0 || *path == '#')
    {
        http_copy(url->path, sizeof url->path, "/");
    }
    else
    {
        const char *fragment = strchr(path, '#');
        size_t length = fragment != NULL ? (size_t)(fragment - path) : strlen(path);

        if (length >= sizeof url->path)
        {
            length = sizeof url->path - 1;
        }

        memcpy(url->path, path, length);
        url->path[length] = 0;
    }

    url_normalize_path(url->path);

    return true;
}

bool http_url_parse(http_url_t *url, const char *text)
{
    if (url == NULL || text == NULL)
    {
        return false;
    }

    memset(url, 0, sizeof *url);

    return url_split(url, text);
}

bool http_url_resolve(http_url_t *url, const char *reference)
{
    char merged[HTTP_PATH_MAX];
    const char *fragment;
    size_t length;

    if (url == NULL || reference == NULL)
    {
        return false;
    }

    reference = http_skip_space(reference);

    if (reference[0] == 0 || reference[0] == '#')
    {
        return true;
    }

    if (http_iprefix(reference, "http://") || http_iprefix(reference, "https://"))
    {
        http_url_t fresh;

        if (!http_url_parse(&fresh, reference))
        {
            return false;
        }

        *url = fresh;

        return true;
    }

    if (strstr(reference, "://") != NULL)
    {
        return false;
    }

    if (reference[0] == '/' && reference[1] == '/')
    {
        char rebuilt[HTTP_URL_MAX];

        http_copy(rebuilt, sizeof rebuilt, url->tls ? "https:" : "http:");
        http_append(rebuilt, sizeof rebuilt, reference);

        return http_url_parse(url, rebuilt);
    }

    fragment = strchr(reference, '#');
    length = fragment != NULL ? (size_t)(fragment - reference) : strlen(reference);

    if (reference[0] == '/')
    {
        if (length >= sizeof merged)
        {
            length = sizeof merged - 1;
        }

        memcpy(merged, reference, length);
        merged[length] = 0;
    }
    else if (reference[0] == '?')
    {
        char *query = strchr(url->path, '?');
        size_t base = query != NULL ? (size_t)(query - url->path) : strlen(url->path);

        if (base >= sizeof merged)
        {
            base = sizeof merged - 1;
        }

        memcpy(merged, url->path, base);
        merged[base] = 0;

        if (base + length < sizeof merged)
        {
            memcpy(&merged[base], reference, length);
            merged[base + length] = 0;
        }
    }
    else
    {
        char *query = strchr(url->path, '?');
        size_t base = query != NULL ? (size_t)(query - url->path) : strlen(url->path);
        size_t cut = 0;
        size_t index;

        for (index = 0; index < base; index++)
        {
            if (url->path[index] == '/')
            {
                cut = index + 1;
            }
        }

        if (cut >= sizeof merged)
        {
            cut = sizeof merged - 1;
        }

        memcpy(merged, url->path, cut);
        merged[cut] = 0;

        if (cut + length < sizeof merged)
        {
            memcpy(&merged[cut], reference, length);
            merged[cut + length] = 0;
        }
    }

    http_copy(url->path, sizeof url->path, merged);
    url_normalize_path(url->path);

    return true;
}

size_t http_url_format(const http_url_t *url, char *out, size_t max)
{
    char port[8];

    if (url == NULL || out == NULL || max == 0)
    {
        return 0;
    }

    http_copy(out, max, url->tls ? "https://" : "http://");
    http_append(out, max, url->host);

    if ((url->tls && url->port != 443) || (!url->tls && url->port != 80))
    {
        http_format_number(port, sizeof port, url->port);
        http_append(out, max, ":");
        http_append(out, max, port);
    }

    return http_append(out, max, url->path);
}

bool http_url_same_origin(const http_url_t *a, const http_url_t *b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    return a->tls == b->tls && a->port == b->port && http_iequal(a->host, b->host);
}

size_t http_url_encode(char *out, size_t max, const char *text, const char *keep)
{
    size_t length = 0;

    if (out == NULL || max == 0)
    {
        return 0;
    }

    out[0] = 0;

    if (text == NULL)
    {
        return 0;
    }

    while (*text != 0)
    {
        char c = *text;

        if (url_unreserved(c, keep))
        {
            if (length + 1 >= max)
            {
                break;
            }

            out[length] = c;
            length++;
        }
        else
        {
            if (length + 3 >= max)
            {
                break;
            }

            out[length] = '%';
            out[length + 1] = url_hex_digit((unsigned)((unsigned char)c >> 4));
            out[length + 2] = url_hex_digit((unsigned)((unsigned char)c & 0x0F));
            length += 3;
        }

        text++;
    }

    out[length] = 0;

    return length;
}

size_t http_url_decode(char *out, size_t max, const char *text, bool plus_is_space)
{
    size_t length = 0;

    if (out == NULL || max == 0)
    {
        return 0;
    }

    out[0] = 0;

    if (text == NULL)
    {
        return 0;
    }

    while (*text != 0 && length + 1 < max)
    {
        char c = *text;

        if (c == '%')
        {
            int high = url_hex_value(text[1]);
            int low = high >= 0 ? url_hex_value(text[2]) : -1;

            if (low >= 0)
            {
                c = (char)((high << 4) | low);
                text += 2;
            }
        }
        else if (plus_is_space && c == '+')
        {
            c = ' ';
        }

        out[length] = c;
        length++;
        text++;
    }

    out[length] = 0;

    return length;
}

void http_form_init(http_form_t *form, char *buffer, size_t capacity)
{
    if (form == NULL)
    {
        return;
    }

    form->buffer = buffer;
    form->capacity = capacity;
    form->length = 0;
    form->overflow = false;

    if (buffer != NULL && capacity > 0)
    {
        buffer[0] = 0;
    }
}

bool http_form_add(http_form_t *form, const char *name, const char *value)
{
    char encoded[96];

    if (form == NULL || form->buffer == NULL || name == NULL)
    {
        return false;
    }

    if (form->length > 0)
    {
        if (form->length + 2 > form->capacity)
        {
            form->overflow = true;
            return false;
        }

        form->buffer[form->length] = '&';
        form->length++;
        form->buffer[form->length] = 0;
    }

    http_url_encode(encoded, sizeof encoded, name, NULL);

    if (form->length + strlen(encoded) + 2 > form->capacity)
    {
        form->overflow = true;
        return false;
    }

    form->length = http_append(form->buffer, form->capacity, encoded);
    form->buffer[form->length] = '=';
    form->length++;
    form->buffer[form->length] = 0;

    http_url_encode(encoded, sizeof encoded, value, NULL);

    if (form->length + strlen(encoded) + 1 > form->capacity)
    {
        form->overflow = true;
        return false;
    }

    form->length = http_append(form->buffer, form->capacity, encoded);

    return true;
}

bool http_form_add_number(http_form_t *form, const char *name, int32_t value)
{
    char text[13];
    size_t offset = 0;
    uint32_t magnitude;

    if (value < 0)
    {
        text[0] = '-';
        offset = 1;
        magnitude = (uint32_t)(-value);
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    http_format_number(&text[offset], sizeof text - offset, magnitude);

    return http_form_add(form, name, text);
}

const char *http_form_data(const http_form_t *form)
{
    if (form == NULL || form->buffer == NULL)
    {
        return "";
    }

    return form->buffer;
}

size_t http_form_length(const http_form_t *form)
{
    return form != NULL ? form->length : 0;
}
