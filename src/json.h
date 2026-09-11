#ifndef SWOOP_JSON_H
#define SWOOP_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    JSON_NONE = 0,
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STRING,
    JSON_NUMBER,
    JSON_TRUE,
    JSON_FALSE,
    JSON_NULL
};

typedef struct {
    const char *start;
    const char *end;
    uint8_t kind;
} json_t;

bool json_value(const char *text, const char *limit, json_t *out);
bool json_member(const json_t *object, const char *name, json_t *out);
bool json_at(const json_t *array, uint16_t index, json_t *out);
uint16_t json_count(const json_t *array);

size_t json_text(const json_t *value, char *out, size_t max);
bool json_text_member(const json_t *object, const char *name, char *out, size_t max);
int32_t json_number(const json_t *value);
int32_t json_number_member(const json_t *object, const char *name, int32_t fallback);
bool json_flag(const json_t *object, const char *name);
bool json_equals(const json_t *value, const char *text);

#endif
