#include "json.h"

#include <string.h>

static const char *skip_space(const char *cursor, const char *limit)
{
    while (cursor < limit && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
                              *cursor == '\n'))
    {
        cursor++;
    }

    return cursor;
}

static const char *skip_string(const char *cursor, const char *limit)
{
    cursor++;

    while (cursor < limit)
    {
        if (*cursor == '\\')
        {
            cursor += 2;
            continue;
        }

        if (*cursor == '"')
        {
            return cursor + 1;
        }

        cursor++;
    }

    return NULL;
}

static const char *skip_value(const char *cursor, const char *limit)
{
    uint8_t depth = 0;

    cursor = skip_space(cursor, limit);

    if (cursor >= limit)
    {
        return NULL;
    }

    if (*cursor == '"')
    {
        return skip_string(cursor, limit);
    }

    if (*cursor != '{' && *cursor != '[')
    {
        while (cursor < limit && *cursor != ',' && *cursor != '}' && *cursor != ']' &&
               *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n')
        {
            cursor++;
        }

        return cursor;
    }

    while (cursor < limit)
    {
        char symbol = *cursor;

        if (symbol == '"')
        {
            cursor = skip_string(cursor, limit);

            if (cursor == NULL)
            {
                return NULL;
            }

            continue;
        }

        if (symbol == '{' || symbol == '[')
        {
            if (depth == 255)
            {
                return NULL;
            }

            depth++;
        }
        else if (symbol == '}' || symbol == ']')
        {
            depth--;

            if (depth == 0)
            {
                return cursor + 1;
            }
        }

        cursor++;
    }

    return NULL;
}

bool json_value(const char *text, const char *limit, json_t *out)
{
    const char *end;

    out->kind = JSON_NONE;
    out->start = NULL;
    out->end = NULL;

    if (text == NULL || limit == NULL || text >= limit)
    {
        return false;
    }

    text = skip_space(text, limit);
    end = skip_value(text, limit);

    if (end == NULL || end <= text)
    {
        return false;
    }

    out->start = text;
    out->end = end;

    switch (*text)
    {
        case '{':
            out->kind = JSON_OBJECT;
            break;

        case '[':
            out->kind = JSON_ARRAY;
            break;

        case '"':
            out->kind = JSON_STRING;
            break;

        case 't':
            out->kind = JSON_TRUE;
            break;

        case 'f':
            out->kind = JSON_FALSE;
            break;

        case 'n':
            out->kind = JSON_NULL;
            break;

        default:
            out->kind = JSON_NUMBER;
            break;
    }

    return true;
}

static bool name_matches(const char *start, const char *end, const char *name)
{
    size_t length = (size_t)(end - start) - 2;

    if (strlen(name) != length)
    {
        return false;
    }

    return memcmp(start + 1, name, length) == 0;
}

bool json_member(const json_t *object, const char *name, json_t *out)
{
    const char *cursor;
    const char *limit;

    out->kind = JSON_NONE;

    if (object == NULL || object->kind != JSON_OBJECT)
    {
        return false;
    }

    cursor = object->start + 1;
    limit = object->end - 1;

    while (cursor < limit)
    {
        const char *key_end;
        bool wanted;

        cursor = skip_space(cursor, limit);

        if (cursor >= limit || *cursor != '"')
        {
            return false;
        }

        key_end = skip_string(cursor, limit);

        if (key_end == NULL)
        {
            return false;
        }

        wanted = name_matches(cursor, key_end, name);

        cursor = skip_space(key_end, limit);

        if (cursor >= limit || *cursor != ':')
        {
            return false;
        }

        cursor++;

        if (!json_value(cursor, limit, out))
        {
            return false;
        }

        if (wanted)
        {
            return true;
        }

        cursor = skip_space(out->end, limit);

        if (cursor < limit && *cursor == ',')
        {
            cursor++;
        }
    }

    out->kind = JSON_NONE;

    return false;
}

bool json_at(const json_t *array, uint16_t index, json_t *out)
{
    const char *cursor;
    const char *limit;
    uint16_t position = 0;

    out->kind = JSON_NONE;

    if (array == NULL || array->kind != JSON_ARRAY)
    {
        return false;
    }

    cursor = array->start + 1;
    limit = array->end - 1;

    while (cursor < limit)
    {
        cursor = skip_space(cursor, limit);

        if (cursor >= limit)
        {
            break;
        }

        if (!json_value(cursor, limit, out))
        {
            break;
        }

        if (position == index)
        {
            return true;
        }

        position++;

        cursor = skip_space(out->end, limit);

        if (cursor < limit && *cursor == ',')
        {
            cursor++;
        }
    }

    out->kind = JSON_NONE;

    return false;
}

uint16_t json_count(const json_t *array)
{
    const char *cursor;
    const char *limit;
    uint16_t total = 0;
    json_t item;

    if (array == NULL || array->kind != JSON_ARRAY)
    {
        return 0;
    }

    cursor = array->start + 1;
    limit = array->end - 1;

    while (cursor < limit)
    {
        cursor = skip_space(cursor, limit);

        if (cursor >= limit || !json_value(cursor, limit, &item))
        {
            break;
        }

        total++;

        cursor = skip_space(item.end, limit);

        if (cursor < limit && *cursor == ',')
        {
            cursor++;
        }
    }

    return total;
}

static uint8_t hex_digit(char symbol)
{
    if (symbol >= '0' && symbol <= '9')
    {
        return (uint8_t)(symbol - '0');
    }

    if (symbol >= 'a' && symbol <= 'f')
    {
        return (uint8_t)(symbol - 'a' + 10);
    }

    if (symbol >= 'A' && symbol <= 'F')
    {
        return (uint8_t)(symbol - 'A' + 10);
    }

    return 0;
}

size_t json_text(const json_t *value, char *out, size_t max)
{
    const char *cursor;
    const char *limit;
    size_t used = 0;

    if (out == NULL || max == 0)
    {
        return 0;
    }

    out[0] = 0;

    if (value == NULL || value->kind != JSON_STRING)
    {
        return 0;
    }

    cursor = value->start + 1;
    limit = value->end - 1;

    while (cursor < limit && used + 1 < max)
    {
        char symbol = *cursor;

        if (symbol != '\\')
        {
            out[used++] = symbol;
            cursor++;
            continue;
        }

        cursor++;

        if (cursor >= limit)
        {
            break;
        }

        switch (*cursor)
        {
            case 'n':
                out[used++] = '\n';
                break;

            case 't':
                out[used++] = ' ';
                break;

            case 'r':
                break;

            case 'b':
            case 'f':
                break;

            case 'u':
            {
                uint16_t point = 0;
                uint8_t digit;

                for (digit = 0; digit < 4 && cursor + 1 + digit < limit; digit++)
                {
                    point = (uint16_t)((point << 4) | hex_digit(cursor[1 + digit]));
                }

                cursor += 4;

                if (point >= 0x20 && point < 0x7F)
                {
                    out[used++] = (char)point;
                }
                else if (point != 0)
                {
                    out[used++] = '?';
                }

                break;
            }

            default:
                out[used++] = *cursor;
                break;
        }

        cursor++;
    }

    out[used] = 0;

    return used;
}

bool json_text_member(const json_t *object, const char *name, char *out, size_t max)
{
    json_t found;

    if (out != NULL && max > 0)
    {
        out[0] = 0;
    }

    if (!json_member(object, name, &found) || found.kind != JSON_STRING)
    {
        return false;
    }

    json_text(&found, out, max);

    return true;
}

int32_t json_number(const json_t *value)
{
    const char *cursor;
    int32_t total = 0;
    bool negative = false;

    if (value == NULL || value->kind != JSON_NUMBER)
    {
        return 0;
    }

    cursor = value->start;

    if (cursor < value->end && *cursor == '-')
    {
        negative = true;
        cursor++;
    }

    while (cursor < value->end && *cursor >= '0' && *cursor <= '9')
    {
        total = total * 10 + (*cursor - '0');
        cursor++;
    }

    return negative ? -total : total;
}

int32_t json_number_member(const json_t *object, const char *name, int32_t fallback)
{
    json_t found;

    if (!json_member(object, name, &found) || found.kind != JSON_NUMBER)
    {
        return fallback;
    }

    return json_number(&found);
}

bool json_flag(const json_t *object, const char *name)
{
    json_t found;

    return json_member(object, name, &found) && found.kind == JSON_TRUE;
}

bool json_equals(const json_t *value, const char *text)
{
    size_t length;

    if (value == NULL || value->kind != JSON_STRING)
    {
        return false;
    }

    length = (size_t)(value->end - value->start) - 2;

    return strlen(text) == length && memcmp(value->start + 1, text, length) == 0;
}
