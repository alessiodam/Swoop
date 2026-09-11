#include "http-client/http_internal.h"

#include <string.h>

static int header_index(const http_headers_t *headers, const char *name)
{
    uint8_t index;

    if (headers == NULL || name == NULL)
    {
        return -1;
    }

    for (index = 0; index < headers->count; index++)
    {
        if (http_iequal(&headers->arena[headers->name[index]], name))
        {
            return (int)index;
        }
    }

    return -1;
}

static void header_drop(http_headers_t *headers, uint8_t index)
{
    uint16_t start = headers->name[index];
    uint16_t stop = (uint16_t)(headers->value[index] +
                               strlen(&headers->arena[headers->value[index]]) + 1);
    uint16_t span = (uint16_t)(stop - start);
    uint8_t scan;

    memmove(&headers->arena[start], &headers->arena[stop],
            (size_t)(headers->used - stop));

    headers->used = (uint16_t)(headers->used - span);

    for (scan = 0; scan < headers->count; scan++)
    {
        if (headers->name[scan] >= stop)
        {
            headers->name[scan] = (uint16_t)(headers->name[scan] - span);
        }

        if (headers->value[scan] >= stop)
        {
            headers->value[scan] = (uint16_t)(headers->value[scan] - span);
        }
    }

    for (scan = (uint8_t)(index + 1); scan < headers->count; scan++)
    {
        headers->name[scan - 1] = headers->name[scan];
        headers->value[scan - 1] = headers->value[scan];
    }

    headers->count--;
}

void http_headers_clear(http_headers_t *headers)
{
    if (headers == NULL)
    {
        return;
    }

    headers->count = 0;
    headers->used = 0;
    headers->truncated = false;
    headers->arena[0] = 0;
}

bool http_headers_add(http_headers_t *headers, const char *name, const char *value)
{
    size_t name_size;
    size_t value_size;

    if (headers == NULL || name == NULL || name[0] == 0)
    {
        return false;
    }

    if (value == NULL)
    {
        value = "";
    }

    if (headers->count >= HTTP_HEADER_FIELDS)
    {
        headers->truncated = true;
        return false;
    }

    name_size = strlen(name) + 1;
    value_size = strlen(value) + 1;

    if ((size_t)headers->used + name_size + value_size > HTTP_HEADER_ARENA)
    {
        headers->truncated = true;
        return false;
    }

    headers->name[headers->count] = headers->used;
    memcpy(&headers->arena[headers->used], name, name_size);
    headers->used = (uint16_t)(headers->used + name_size);

    headers->value[headers->count] = headers->used;
    memcpy(&headers->arena[headers->used], value, value_size);
    headers->used = (uint16_t)(headers->used + value_size);

    headers->count++;

    return true;
}

bool http_headers_set(http_headers_t *headers, const char *name, const char *value)
{
    http_headers_remove(headers, name);

    return http_headers_add(headers, name, value);
}

bool http_headers_set_number(http_headers_t *headers, const char *name, uint32_t value)
{
    char text[12];

    http_format_number(text, sizeof text, value);

    return http_headers_set(headers, name, text);
}

void http_headers_remove(http_headers_t *headers, const char *name)
{
    int index = header_index(headers, name);

    while (index >= 0)
    {
        header_drop(headers, (uint8_t)index);
        index = header_index(headers, name);
    }
}

const char *http_headers_get(const http_headers_t *headers, const char *name)
{
    int index = header_index(headers, name);

    if (index < 0)
    {
        return NULL;
    }

    return &headers->arena[headers->value[index]];
}

bool http_headers_has(const http_headers_t *headers, const char *name)
{
    return header_index(headers, name) >= 0;
}

uint8_t http_headers_count(const http_headers_t *headers)
{
    return headers != NULL ? headers->count : 0;
}

const char *http_headers_name_at(const http_headers_t *headers, uint8_t index)
{
    if (headers == NULL || index >= headers->count)
    {
        return NULL;
    }

    return &headers->arena[headers->name[index]];
}

const char *http_headers_value_at(const http_headers_t *headers, uint8_t index)
{
    if (headers == NULL || index >= headers->count)
    {
        return NULL;
    }

    return &headers->arena[headers->value[index]];
}

bool http_headers_truncated(const http_headers_t *headers)
{
    return headers != NULL && headers->truncated;
}
