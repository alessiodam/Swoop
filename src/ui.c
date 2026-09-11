#include "swoop.h"

#include <graphx.h>
#include <string.h>

/* In the order of the enum in swoop.h, and the sites' dark theme exactly. */
static const uint8_t palette_table[COLOR_COUNT][3] = {
    { 13, 14, 16 },
    { 26, 28, 30 },
    { 36, 38, 40 },
    { 60, 63, 67 },
    { 236, 238, 240 },
    { 154, 160, 166 },
    { 107, 112, 118 },
    { 34, 211, 238 },
    { 34, 211, 238 },
    { 76, 187, 111 },
    { 217, 166, 62 },
    { 240, 113, 106 }
};

static const char *chrome_title;
static const char *chrome_footer;

void ui_palette(void)
{
    uint8_t index;

    for (index = 0; index < COLOR_COUNT; index++)
    {
        gfx_palette[index] = gfx_RGBTo1555(palette_table[index][0],
                                           palette_table[index][1],
                                           palette_table[index][2]);
    }

    gfx_SetTextTransparentColor(COLOR_COUNT);
    gfx_SetTextBGColor(COLOR_COUNT);
}

void ui_text(int x, int y, const char *text, uint8_t color)
{
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(text, x, y);
}

void ui_clipped(int x, int y, const char *text, uint8_t color, uint8_t columns)
{
    char buffer[COLS + 1];
    size_t length = strlen(text);

    if (columns > COLS)
    {
        columns = COLS;
    }

    if (length > columns)
    {
        length = columns;
    }

    memcpy(buffer, text, length);
    buffer[length] = 0;

    ui_text(x, y, buffer, color);
}

void ui_right(const char *text, int y, uint8_t color)
{
    ui_text(SCREEN_W - 3 - (int)gfx_GetStringWidth(text), y, text, color);
}

void ui_center(int center, int y, const char *text, uint8_t color)
{
    ui_text(center - (int)gfx_GetStringWidth(text) / 2, y, text, color);
}

void ui_number(char *out, uint32_t value)
{
    char digits[11];
    uint8_t count = 0;
    uint8_t index = 0;

    do
    {
        digits[count] = (char)('0' + (value % 10u));
        value /= 10u;
        count++;
    }
    while (value != 0 && count < sizeof digits);

    while (count > 0)
    {
        count--;
        out[index] = digits[count];
        index++;
    }

    out[index] = 0;
}

void ui_size(char *out, uint32_t bytes)
{
    char part[12];

    if (bytes < 1024u)
    {
        ui_number(part, bytes);
        strcpy(out, part);
        strcat(out, " B");
        return;
    }

    ui_number(part, bytes / 1024u);
    strcpy(out, part);
    strcat(out, ".");

    ui_number(part, (bytes % 1024u) * 10u / 1024u);
    strcat(out, part);
    strcat(out, " KB");
}

void ui_row(int y, int height, bool selected)
{
    if (!selected)
    {
        return;
    }

    gfx_SetColor(COLOR_HOVER);
    gfx_FillRectangle(0, y, SCREEN_W - 4, height);

    gfx_SetColor(COLOR_ACCENT_FILL);
    gfx_FillRectangle(0, y, 2, height);
}

/* State is a word in the colour that means it. Nothing here is a pill. */
void ui_tag(int x, int y, const char *text, uint8_t color)
{
    ui_text(x, y, text, color);
}

/* The square the sites use for the brand and for a status dot. */
void ui_mark(int x, int y, uint8_t color)
{
    gfx_SetColor(color);
    gfx_FillRectangle(x - 2, y - 2, 5, 5);
}

/* The mark and the name, centred, as the rail draws them on the sites. */
void ui_brand(int center, int y)
{
    int width;
    int x;

    gfx_SetTextScale(2, 2);

    width = (int)gfx_GetStringWidth("Swoop");
    x = center - (width + 14) / 2;

    gfx_SetColor(COLOR_ACCENT_FILL);
    gfx_FillRectangle(x, y + 4, 8, 8);

    ui_text(x + 14, y, "Swoop", COLOR_TEXT);

    gfx_SetTextScale(1, 1);
}

void ui_rule(int x, int y, int w)
{
    gfx_SetColor(COLOR_RULE);
    gfx_HorizLine(x, y, w);
}

/* Only a layer that floats over the page is boxed, and then only by a rule. */
void ui_panel(int x, int y, int w, int h)
{
    gfx_SetColor(COLOR_BG);
    gfx_FillRectangle(x, y, w, h);

    gfx_SetColor(COLOR_RULE);
    gfx_Rectangle(x, y, w, h);
}

void ui_bar(int x, int y, int w, int h, uint32_t done, uint32_t total)
{
    int filled;

    gfx_SetColor(COLOR_BG);
    gfx_FillRectangle(x, y, w, h);

    gfx_SetColor(COLOR_RULE);
    gfx_Rectangle(x, y, w, h);

    if (total == 0 || done == 0)
    {
        return;
    }

    if (done > total)
    {
        done = total;
    }

    filled = (int)((uint32_t)(w - 2) * done / total);

    gfx_SetColor(COLOR_ACCENT_FILL);
    gfx_FillRectangle(x + 1, y + 1, filled, h - 2);
}

void ui_frame(const char *title, const char *footer)
{
    gfx_FillScreen(COLOR_BG);

    chrome_title = title;
    chrome_footer = footer;
}

void ui_overlay(void)
{
    if (chrome_footer == NULL && chrome_title == NULL)
    {
        return;
    }

    gfx_SetColor(COLOR_BG);
    gfx_FillRectangle(0, 0, SCREEN_W, HEADER_H);
    gfx_FillRectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H);

    gfx_SetColor(COLOR_RULE);
    gfx_HorizLine(0, HEADER_H, SCREEN_W);
    gfx_HorizLine(0, SCREEN_H - FOOTER_H - 1, SCREEN_W);

    ui_clipped(4, 3, chrome_title != NULL ? chrome_title : "", COLOR_TEXT, 32);

    ui_mark(SCREEN_W - 8, 7, witi_wifi_online() ? COLOR_GOOD : COLOR_FAINT);

    ui_clipped(4, SCREEN_H - FOOTER_H + 2,
               chrome_footer != NULL ? chrome_footer : "", COLOR_FAINT, 36);

    if (net_busy())
    {
        ui_mark(SCREEN_W - 8, SCREEN_H - FOOTER_H + 5, COLOR_ACCENT_FILL);
    }

    chrome_title = NULL;
    chrome_footer = NULL;
}

void ui_scroll(uint8_t selected, uint8_t count, uint8_t *top, uint8_t rows)
{
    if (count == 0 || rows == 0)
    {
        *top = 0;
        return;
    }

    if (selected < *top)
    {
        *top = selected;
    }

    if (selected >= *top + rows)
    {
        *top = (uint8_t)(selected - rows + 1);
    }
}

void ui_scrollbar(uint16_t top, uint16_t visible, uint16_t total)
{
    int track = BODY_H;
    int height;
    int offset;

    if (total <= visible || total == 0)
    {
        return;
    }

    height = track * visible / total;

    if (height < 8)
    {
        height = 8;
    }

    offset = (track - height) * top / (total - visible);

    gfx_SetColor(COLOR_RULE_STRONG);
    gfx_FillRectangle(SCREEN_W - 3, BODY_TOP + offset, 2, height);
}

void ui_empty(const char *first, const char *second)
{
    ui_center(SCREEN_W / 2, BODY_TOP + BODY_H / 2 - 12, first, COLOR_DIM);

    if (second != NULL)
    {
        ui_center(SCREEN_W / 2, BODY_TOP + BODY_H / 2 + 2, second, COLOR_DIM);
    }
}

uint8_t ui_wrap(const char *text, uint16_t *starts, uint8_t *lengths,
                uint8_t max_rows, uint8_t columns)
{
    uint16_t length = (uint16_t)strlen(text);
    uint16_t offset = 0;
    uint8_t rows = 0;

    while (offset < length && rows < max_rows)
    {
        uint16_t room = (uint16_t)(length - offset);
        uint8_t take;
        uint16_t scan;
        uint16_t stop = offset;

        while (stop < length && text[stop] != '\n')
        {
            stop++;
        }

        room = (uint16_t)(stop - offset);

        if (room == 0)
        {
            starts[rows] = offset;
            lengths[rows] = 0;
            rows++;
            offset = stop + 1;
            continue;
        }

        if (room <= columns)
        {
            take = (uint8_t)room;
        }
        else
        {
            take = columns;

            for (scan = take; scan > (uint16_t)(columns / 3); scan--)
            {
                if (text[offset + scan] == ' ')
                {
                    take = (uint8_t)scan;
                    break;
                }
            }
        }

        starts[rows] = offset;
        lengths[rows] = take;
        rows++;

        offset = (uint16_t)(offset + take);

        while (offset < length && text[offset] == ' ')
        {
            offset++;
        }

        if (offset == stop && stop < length)
        {
            offset = stop + 1;
        }
    }

    return rows;
}
