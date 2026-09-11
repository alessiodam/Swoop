#ifndef SWOOP_INPUT_H
#define SWOOP_INPUT_H

#include <keypadc.h>
#include <stdbool.h>
#include <stdint.h>

enum {
    ENTRY_LOWER = 0,
    ENTRY_UPPER,
    ENTRY_SYMBOL
};

typedef struct {
    char *buffer;
    uint16_t capacity;
    uint16_t length;
    uint16_t cursor;
    uint8_t mode;
    uint8_t pick_row;
    uint8_t pick_col;
    bool picker;
} entry_t;

void keys_init(void);
void keys_poll(void);
bool key_pressed(kb_lkey_t key);
bool key_held(kb_lkey_t key);
bool key_repeat(kb_lkey_t key);
bool key_any(void);

void entry_begin(entry_t *entry, char *buffer, uint16_t capacity, const char *initial);
bool entry_update(entry_t *entry);
bool entry_busy(const entry_t *entry);
void entry_insert_text(entry_t *entry, const char *text);
void entry_draw_field(const entry_t *entry, int x, int y, int w, bool mask);
void entry_draw_overlay(const entry_t *entry);
const char *entry_mode_name(const entry_t *entry);

#endif
