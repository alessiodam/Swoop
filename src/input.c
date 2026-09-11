#include "input.h"

#include "swoop.h"

#include <graphx.h>
#include <string.h>

#define KEY_ROWS 8
#define REPEAT_DELAY 20
#define REPEAT_RATE 3

#define PICK_COLS 11
#define PICK_ROWS 4
#define PICK_CELL_W 20
#define PICK_CELL_H 16
#define PICK_W (PICK_COLS * PICK_CELL_W + 8)
#define PICK_H (PICK_ROWS * PICK_CELL_H + 32)
#define PICK_X ((SCREEN_W - PICK_W) / 2)
#define PICK_Y ((SCREEN_H - PICK_H) / 2)

typedef struct {
    kb_lkey_t key;
    char letter;
    char symbol;
} key_entry_t;

static const key_entry_t key_table[] = {
    { kb_KeyMath, 'a', ':' },
    { kb_KeyApps, 'b', '~' },
    { kb_KeyPrgm, 'c', '?' },
    { kb_KeyRecip, 'd', '"' },
    { kb_KeySin, 'e', '!' },
    { kb_KeyCos, 'f', '&' },
    { kb_KeyTan, 'g', '$' },
    { kb_KeyPower, 'h', '^' },
    { kb_KeySquare, 'i', '@' },
    { kb_KeyComma, 'j', ',' },
    { kb_KeyLParen, 'k', '(' },
    { kb_KeyRParen, 'l', ')' },
    { kb_KeyDiv, 'm', '/' },
    { kb_KeyLog, 'n', '#' },
    { kb_Key7, 'o', '7' },
    { kb_Key8, 'p', '8' },
    { kb_Key9, 'q', '9' },
    { kb_KeyMul, 'r', '*' },
    { kb_KeyLn, 's', '%' },
    { kb_Key4, 't', '4' },
    { kb_Key5, 'u', '5' },
    { kb_Key6, 'v', '6' },
    { kb_KeySub, 'w', '-' },
    { kb_KeySto, 'x', '_' },
    { kb_Key1, 'y', '1' },
    { kb_Key2, 'z', '2' },
    { kb_Key3, '/', '3' },
    { kb_KeyAdd, '"', '+' },
    { kb_Key0, ' ', '0' },
    { kb_KeyDecPnt, ':', '.' },
    { kb_KeyChs, '?', '-' },
    { kb_KeyVars, '=', '=' },
    { kb_KeyGraphVar, '<', '>' }
};

#define KEY_COUNT (sizeof key_table / sizeof key_table[0])

static const char pick_chars[] =
    " !\"#$%&'()*"
    "+,-./:;<=>?"
    "@[\\]^_`{|}~"
    "0123456789";

#define PICK_COUNT (sizeof pick_chars - 1)

static uint8_t previous[KEY_ROWS];
static uint8_t current[KEY_ROWS];
static uint8_t hold[KEY_ROWS][8];

static uint8_t key_group(kb_lkey_t key)
{
    return (uint8_t)(key >> 8);
}

static uint8_t key_mask(kb_lkey_t key)
{
    return (uint8_t)(key & 0xFFu);
}

static uint8_t mask_bit(uint8_t mask)
{
    uint8_t bit = 0;

    while (bit < 8 && (mask & (uint8_t)(1u << bit)) == 0)
    {
        bit++;
    }

    return bit;
}

void keys_init(void)
{
    memset(previous, 0, sizeof previous);
    memset(current, 0, sizeof current);
    memset(hold, 0, sizeof hold);
}

void keys_poll(void)
{
    uint8_t group;
    uint8_t bit;

    kb_Scan();

    memcpy(previous, current, sizeof previous);

    for (group = 0; group < KEY_ROWS; group++)
    {
        current[group] = (uint8_t)kb_Data[group];

        for (bit = 0; bit < 8; bit++)
        {
            if ((current[group] & (uint8_t)(1u << bit)) != 0)
            {
                if (hold[group][bit] < 255)
                {
                    hold[group][bit]++;
                }
            }
            else
            {
                hold[group][bit] = 0;
            }
        }
    }
}

bool key_pressed(kb_lkey_t key)
{
    uint8_t group = key_group(key);
    uint8_t mask = key_mask(key);

    return (current[group] & mask) != 0 && (previous[group] & mask) == 0;
}

bool key_held(kb_lkey_t key)
{
    return (current[key_group(key)] & key_mask(key)) != 0;
}

bool key_repeat(kb_lkey_t key)
{
    uint8_t group = key_group(key);
    uint8_t mask = key_mask(key);
    uint8_t count;

    if (key_pressed(key))
    {
        return true;
    }

    if ((current[group] & mask) == 0)
    {
        return false;
    }

    count = hold[group][mask_bit(mask)];

    if (count < REPEAT_DELAY)
    {
        return false;
    }

    return ((count - REPEAT_DELAY) % REPEAT_RATE) == 0;
}

bool key_any(void)
{
    uint8_t group;

    for (group = 0; group < KEY_ROWS; group++)
    {
        if (current[group] != 0)
        {
            return true;
        }
    }

    return false;
}

static char entry_char(const entry_t *entry)
{
    unsigned int index;

    for (index = 0; index < KEY_COUNT; index++)
    {
        if (!key_pressed(key_table[index].key))
        {
            continue;
        }

        if (entry->mode == ENTRY_SYMBOL)
        {
            return key_table[index].symbol;
        }

        if (entry->mode == ENTRY_UPPER && key_table[index].letter >= 'a' &&
            key_table[index].letter <= 'z')
        {
            return (char)(key_table[index].letter - ('a' - 'A'));
        }

        return key_table[index].letter;
    }

    return 0;
}

static void entry_insert(entry_t *entry, char c)
{
    uint16_t index;

    if (c == 0 || entry->length + 1u >= entry->capacity)
    {
        return;
    }

    for (index = entry->length; index > entry->cursor; index--)
    {
        entry->buffer[index] = entry->buffer[index - 1];
    }

    entry->buffer[entry->cursor] = c;
    entry->cursor++;
    entry->length++;
    entry->buffer[entry->length] = 0;
}

static void entry_backspace(entry_t *entry)
{
    uint16_t index;

    if (entry->cursor == 0)
    {
        return;
    }

    for (index = (uint16_t)(entry->cursor - 1); index + 1 < entry->length; index++)
    {
        entry->buffer[index] = entry->buffer[index + 1];
    }

    entry->cursor--;
    entry->length--;
    entry->buffer[entry->length] = 0;
}

void entry_insert_text(entry_t *entry, const char *text)
{
    while (*text != 0)
    {
        entry_insert(entry, *text);
        text++;
    }
}

static uint8_t pick_index(const entry_t *entry)
{
    return (uint8_t)(entry->pick_row * PICK_COLS + entry->pick_col);
}

static bool picker_update(entry_t *entry)
{
    if (key_pressed(kb_KeyClear) || key_pressed(kb_Key2nd) || key_pressed(kb_KeyAlpha))
    {
        entry->picker = false;
        return true;
    }

    if (key_repeat(kb_KeyLeft) && entry->pick_col > 0)
    {
        entry->pick_col--;
    }

    if (key_repeat(kb_KeyRight) && entry->pick_col + 1u < PICK_COLS)
    {
        entry->pick_col++;
    }

    if (key_repeat(kb_KeyUp) && entry->pick_row > 0)
    {
        entry->pick_row--;
    }

    if (key_repeat(kb_KeyDown) && entry->pick_row + 1u < PICK_ROWS)
    {
        entry->pick_row++;
    }

    if (pick_index(entry) >= PICK_COUNT)
    {
        entry->pick_col = (uint8_t)(PICK_COUNT - entry->pick_row * PICK_COLS - 1);
    }

    if (key_repeat(kb_KeyDel))
    {
        entry_backspace(entry);
    }

    if (key_pressed(kb_KeyEnter))
    {
        entry_insert(entry, pick_chars[pick_index(entry)]);
    }

    return true;
}

void entry_begin(entry_t *entry, char *buffer, uint16_t capacity, const char *initial)
{
    entry->buffer = buffer;
    entry->capacity = capacity;
    entry->mode = ENTRY_LOWER;
    entry->picker = false;
    entry->pick_row = 0;
    entry->pick_col = 0;

    if (initial != NULL && initial != buffer)
    {
        strncpy(buffer, initial, capacity - 1);
        buffer[capacity - 1] = 0;
    }

    entry->length = (uint16_t)strlen(buffer);
    entry->cursor = entry->length;
}

bool entry_busy(const entry_t *entry)
{
    return entry->picker;
}

bool entry_update(entry_t *entry)
{
    char c;

    if (entry->picker)
    {
        return picker_update(entry);
    }

    if (key_pressed(kb_Key2nd))
    {
        entry->picker = true;
        return true;
    }

    if (key_pressed(kb_KeyAlpha))
    {
        entry->mode = (uint8_t)((entry->mode + 1) % 3);
        return true;
    }

    if (key_pressed(kb_KeyMode))
    {
        entry->length = 0;
        entry->cursor = 0;
        entry->buffer[0] = 0;
        return true;
    }

    if (key_repeat(kb_KeyLeft))
    {
        if (entry->cursor > 0)
        {
            entry->cursor--;
        }

        return true;
    }

    if (key_repeat(kb_KeyRight))
    {
        if (entry->cursor < entry->length)
        {
            entry->cursor++;
        }

        return true;
    }

    if (key_pressed(kb_KeyUp))
    {
        entry->cursor = 0;
        return true;
    }

    if (key_pressed(kb_KeyDown))
    {
        entry->cursor = entry->length;
        return true;
    }

    if (key_repeat(kb_KeyDel))
    {
        entry_backspace(entry);
        return true;
    }

    c = entry_char(entry);

    if (c != 0)
    {
        entry_insert(entry, c);
        return true;
    }

    return false;
}

void entry_draw_field(const entry_t *entry, int x, int y, int w, bool mask)
{
    char view[COLS + 1];
    uint16_t columns = (uint16_t)((w - 8) / COL_W);
    uint16_t first;
    uint16_t index;

    if (columns > COLS)
    {
        columns = COLS;
    }

    first = entry->cursor >= columns ? (uint16_t)(entry->cursor - columns + 1) : 0;

    for (index = 0; index < columns && first + index < entry->length; index++)
    {
        view[index] = mask ? '*' : entry->buffer[first + index];
    }

    view[index] = 0;

    gfx_SetColor(COLOR_RULE_STRONG);
    gfx_Rectangle(x, y, w, 14);

    ui_text(x + 4, y + 3, view, COLOR_TEXT);

    gfx_SetColor(COLOR_ACCENT_FILL);
    gfx_VertLine(x + 4 + (int)(entry->cursor - first) * COL_W, y + 2, 10);
}

void entry_draw_overlay(const entry_t *entry)
{
    uint8_t row;
    uint8_t column;
    char cell[2];

    if (!entry->picker)
    {
        return;
    }

    gfx_SetColor(COLOR_BG);
    gfx_FillRectangle(PICK_X, PICK_Y, PICK_W, PICK_H);

    gfx_SetColor(COLOR_RULE);
    gfx_Rectangle(PICK_X, PICK_Y, PICK_W, PICK_H);

    ui_text(PICK_X + 4, PICK_Y + 4, "symbols", COLOR_FAINT);

    cell[1] = 0;

    for (row = 0; row < PICK_ROWS; row++)
    {
        for (column = 0; column < PICK_COLS; column++)
        {
            uint8_t index = (uint8_t)(row * PICK_COLS + column);
            int cx = PICK_X + 4 + column * PICK_CELL_W;
            int cy = PICK_Y + 18 + row * PICK_CELL_H;
            bool chosen = row == entry->pick_row && column == entry->pick_col;

            if (index >= PICK_COUNT)
            {
                break;
            }

            if (chosen)
            {
                gfx_SetColor(COLOR_TEXT);
                gfx_FillRectangle(cx, cy - 2, PICK_CELL_W, PICK_CELL_H);
            }

            if (pick_chars[index] == ' ')
            {
                ui_text(cx + 2, cy, "sp", chosen ? COLOR_BG : COLOR_DIM);
            }
            else
            {
                cell[0] = pick_chars[index];
                ui_text(cx + 6, cy, cell, chosen ? COLOR_BG : COLOR_TEXT);
            }
        }
    }

    ui_text(PICK_X + 4, PICK_Y + PICK_H - 12, "enter add   2nd close", COLOR_DIM);
}

const char *entry_mode_name(const entry_t *entry)
{
    if (entry->picker)
    {
        return "sym";
    }

    switch (entry->mode)
    {
        case ENTRY_UPPER:
            return "ABC";

        case ENTRY_SYMBOL:
            return "123";

        default:
            break;
    }

    return "abc";
}
