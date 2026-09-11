#ifndef HTTP_INTERNAL_H
#define HTTP_INTERNAL_H

#include "http-client/http_client.h"

int http_lower(int c);
bool http_iequal(const char *a, const char *b);
bool http_iprefix(const char *text, const char *prefix);
const char *http_skip_space(const char *text);
void http_trim(char *text);
size_t http_copy(char *out, size_t max, const char *text);
size_t http_append(char *out, size_t max, const char *text);
uint32_t http_parse_number(const char *text, bool *valid);
uint32_t http_parse_hex(const char *text, bool *valid);
size_t http_format_number(char *out, size_t max, uint32_t value);
bool http_token_present(const char *list, const char *token);

#endif
