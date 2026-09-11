#include "http-client/http_internal.h"

#include <string.h>

int http_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c + ('a' - 'A');
    }

    return c;
}

bool http_iequal(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    while (*a != 0 && *b != 0)
    {
        if (http_lower((unsigned char)*a) != http_lower((unsigned char)*b))
        {
            return false;
        }

        a++;
        b++;
    }

    return *a == *b;
}

bool http_iprefix(const char *text, const char *prefix)
{
    if (text == NULL || prefix == NULL)
    {
        return false;
    }

    while (*prefix != 0)
    {
        if (http_lower((unsigned char)*text) != http_lower((unsigned char)*prefix))
        {
            return false;
        }

        text++;
        prefix++;
    }

    return true;
}

const char *http_skip_space(const char *text)
{
    while (*text == ' ' || *text == '\t')
    {
        text++;
    }

    return text;
}

void http_trim(char *text)
{
    size_t length;
    size_t start = 0;

    if (text == NULL)
    {
        return;
    }

    while (text[start] == ' ' || text[start] == '\t')
    {
        start++;
    }

    if (start > 0)
    {
        memmove(text, text + start, strlen(text + start) + 1);
    }

    length = strlen(text);

    while (length > 0)
    {
        char last = text[length - 1];

        if (last != ' ' && last != '\t' && last != '\r' && last != '\n')
        {
            break;
        }

        length--;
    }

    text[length] = 0;
}

size_t http_copy(char *out, size_t max, const char *text)
{
    size_t length = 0;

    if (out == NULL || max == 0)
    {
        return 0;
    }

    if (text != NULL)
    {
        while (text[length] != 0 && length + 1 < max)
        {
            out[length] = text[length];
            length++;
        }
    }

    out[length] = 0;

    return length;
}

size_t http_append(char *out, size_t max, const char *text)
{
    size_t length;

    if (out == NULL || max == 0)
    {
        return 0;
    }

    length = strlen(out);

    if (text != NULL)
    {
        while (*text != 0 && length + 1 < max)
        {
            out[length] = *text;
            length++;
            text++;
        }
    }

    out[length] = 0;

    return length;
}

uint32_t http_parse_number(const char *text, bool *valid)
{
    uint32_t value = 0;
    bool seen = false;

    if (text == NULL)
    {
        if (valid != NULL)
        {
            *valid = false;
        }

        return 0;
    }

    text = http_skip_space(text);

    while (*text >= '0' && *text <= '9')
    {
        value = value * 10u + (uint32_t)(*text - '0');
        seen = true;
        text++;
    }

    if (valid != NULL)
    {
        *valid = seen;
    }

    return value;
}

uint32_t http_parse_hex(const char *text, bool *valid)
{
    uint32_t value = 0;
    bool seen = false;

    if (text == NULL)
    {
        if (valid != NULL)
        {
            *valid = false;
        }

        return 0;
    }

    text = http_skip_space(text);

    for (;;)
    {
        int c = http_lower((unsigned char)*text);
        uint32_t digit;

        if (c >= '0' && c <= '9')
        {
            digit = (uint32_t)(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = (uint32_t)(c - 'a') + 10u;
        }
        else
        {
            break;
        }

        value = (value << 4) | digit;
        seen = true;
        text++;
    }

    if (valid != NULL)
    {
        *valid = seen;
    }

    return value;
}

size_t http_format_number(char *out, size_t max, uint32_t value)
{
    char digits[12];
    size_t count = 0;
    size_t index;

    if (out == NULL || max == 0)
    {
        return 0;
    }

    do
    {
        digits[count] = (char)('0' + (value % 10u));
        value /= 10u;
        count++;
    }
    while (value != 0 && count < sizeof digits);

    index = 0;

    while (count > 0 && index + 1 < max)
    {
        count--;
        out[index] = digits[count];
        index++;
    }

    out[index] = 0;

    return index;
}

bool http_token_present(const char *list, const char *token)
{
    size_t length;

    if (list == NULL || token == NULL)
    {
        return false;
    }

    length = strlen(token);

    while (*list != 0)
    {
        const char *start;
        size_t span = 0;

        while (*list == ' ' || *list == '\t' || *list == ',')
        {
            list++;
        }

        if (*list == 0)
        {
            break;
        }

        start = list;

        while (list[span] != 0 && list[span] != ',')
        {
            span++;
        }

        list += span;

        while (span > 0 && (start[span - 1] == ' ' || start[span - 1] == '\t'))
        {
            span--;
        }

        if (span == length)
        {
            size_t index = 0;

            while (index < span &&
                   http_lower((unsigned char)start[index]) ==
                       http_lower((unsigned char)token[index]))
            {
                index++;
            }

            if (index == span)
            {
                return true;
            }
        }
    }

    return false;
}
