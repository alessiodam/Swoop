#ifndef SWOOP_H
#define SWOOP_H

#include "json.h"
#include "net.h"

#include <witi.h>

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define HEADER_H 14
#define FOOTER_H 12
#define BODY_TOP (HEADER_H + 3)
#define BODY_BOTTOM (SCREEN_H - FOOTER_H - 2)
#define BODY_H (BODY_BOTTOM - BODY_TOP)
#define LINE_H 9
#define ROW_H 11
#define COL_W 8
#define COLS 39
#define BODY_LINES (BODY_H / LINE_H)
#define LIST_ROWS (BODY_H / ROW_H)

#define SLUG_MAX 50
#define TITLE_MAX 42
#define SUMMARY_MAX 60
#define AUTHOR_MAX 24
#define VERSION_MAX 16
#define VARNAME_MAX 9
#define CATEGORY_MAX 14
#define DESC_MAX 700
#define DESC_ROWS 96

#define PROGRAM_MAX 12
#define CATALOG_MAX 10
#define REQUIRE_MAX 16
#define QUERY_MAX 32
#define VERSION_TEXT 16
#define DIGEST_TEXT 17
#define INSTALL_MAX 16

#define STORE_NAME "SWOOP"
#define WITICFG_NAME "WITICFG"
#define HANDOFF_DELAY_MS 3000u
#define JOIN_TIMEOUT_MS 25000u
#define STORE_VERSION 1
#define SWOOP_RELEASE "1.0.0"
#define SWOOP_AGENT "Swoop/" SWOOP_RELEASE " (TI-84 Plus CE)"

#define ROOST_URL "https://roost.ceagle.cc"

#define VAR_PROGRAM 0
#define VAR_PROTECTED 1
#define VAR_APPVAR 2

/*
 * The palette the CEagle sites use, carried onto the calculator. Grey does the
 * work; the accent marks one thing at a time, which here means the row you are
 * on and the work that is moving. Colour otherwise only ever means state.
 *
 * The sites follow whichever theme the reader set. The calculator has nobody to
 * ask, so it takes the dark one and stays there.
 *
 * Kept in step with the dark block of shared/webui/static/base.css in the
 * ceagle repository. The accent and its fill are one colour there, and stay
 * two names here because the two mean different things.
 */
enum {
    COLOR_BG = 0,      /* canvas      #0d0e10 */
    COLOR_HOVER,       /* hover       #1a1c1e, the row you are on */
    COLOR_RULE,        /* rule        #242628 */
    COLOR_RULE_STRONG, /* rule-strong #3c3f43 */
    COLOR_TEXT,        /* ink         #eceef0 */
    COLOR_DIM,         /* muted       #9aa0a6 */
    COLOR_FAINT,       /* faint       #6b7076 */
    COLOR_ACCENT,      /* accent      #22d3ee, reads against the canvas */
    COLOR_ACCENT_FILL, /* accent-fill #22d3ee, for marks and bars */
    COLOR_GOOD,        /* ok          #4cbb6f */
    COLOR_WARN,        /* warn        #d9a63e */
    COLOR_BAD,         /* bad         #f0716a */
    COLOR_COUNT
};

enum {
    VIEW_LIST = 0,
    VIEW_DETAIL,
    VIEW_SEARCH,
    VIEW_FILTER,
    VIEW_PLAN,
    VIEW_INSTALL,
    VIEW_INSTALLED,
    VIEW_UPDATES,
    VIEW_MENU,
    VIEW_ABOUT
};

enum {
    LANG_ANY = 0,
    LANG_BASIC,
    LANG_ASM,
    LANG_LIB
};

#define LANG_COUNT 4

enum {
    SORT_NEWEST = 0,
    SORT_DOWNLOADS,
    SORT_TITLE,
    SORT_SIZE,
    SORT_UPDATED,
    SORT_COUNT
};

enum {
    STATE_UNKNOWN = 0,
    STATE_CURRENT,
    STATE_UPDATE,
    STATE_GONE,
    STATE_ABSENT,
    STATE_CHANGED
};

enum {
    JOB_IDLE = 0,
    JOB_RUNNING,
    JOB_WRITING,
    JOB_DONE,
    JOB_FAILED
};

enum {
    WANT_NOTHING = 0,
    WANT_REQUIRES,
    WANT_ITEM
};

typedef struct {
    char slug[SLUG_MAX];
    char title[TITLE_MAX];
    char summary[SUMMARY_MAX];
    char author[AUTHOR_MAX];
    char category[CATEGORY_MAX];
    char var_name[VARNAME_MAX];
    char version[VERSION_TEXT];
    char digest[DIGEST_TEXT];
    uint32_t size;
    uint32_t downloads;
    uint8_t language;
    uint8_t var_type;
    bool archived;
    bool installed;
} program_t;

typedef struct {
    char slug[SLUG_MAX];
    char title[TITLE_MAX];
    char var_name[VARNAME_MAX];
    char version[VERSION_TEXT];
    char digest[DIGEST_TEXT];
    uint32_t size;
    uint8_t var_type;
    bool archived;
} install_t;

typedef struct {
    char slug[SLUG_MAX];
    char title[TITLE_MAX];
    char have[VERSION_TEXT];
    char want[VERSION_TEXT];
    char digest[DIGEST_TEXT];
    char var_name[VARNAME_MAX];
    uint32_t size;
    uint8_t var_type;
    uint8_t state;
    bool archived;
    bool chosen;
} update_t;

typedef struct {
    char key[CATEGORY_MAX];
    char name[CATEGORY_MAX + 8];
    uint16_t count;
} catalog_t;

typedef struct {
    uint8_t version;
    uint8_t language;
    uint8_t sort;
    char category[CATEGORY_MAX];
    uint8_t install_count;
    install_t installs[INSTALL_MAX];
} store_t;

typedef struct {
    program_t record;
    char slug[SLUG_MAX];
    char var_name[VARNAME_MAX];
    uint8_t var_type;
    bool archived;
    uint32_t expected;
    uint32_t written;
    uint8_t state;
    uint8_t handle;
    uint8_t origin;
    bool opened;
    bool settled;
    char message[48];

    /*
     * A program that names dependencies arrives as a short queue: every
     * library first, in the order the server flattened them, and the program
     * the reader actually picked last. One transfer runs at a time.
     */
    install_t queue[REQUIRE_MAX];
    uint8_t queue_count;
    uint8_t queue_index;
    uint8_t want;
    bool dependency;
    bool overflow;
} job_t;

extern store_t store;
extern bool store_dirty;

extern program_t programs[PROGRAM_MAX];
extern uint8_t program_count;
extern uint8_t program_index;
extern uint16_t program_total;
extern uint16_t program_page;
extern uint16_t program_pages;

extern catalog_t catalog[CATALOG_MAX];
extern uint8_t catalog_count;

extern program_t detail;
extern char detail_text[DESC_MAX];
extern bool detail_ready;

extern install_t requirements[REQUIRE_MAX];
extern uint8_t require_count;
extern bool require_overflow;

extern job_t job;

extern update_t updates[INSTALL_MAX];
extern uint8_t update_count;
extern uint8_t update_index;
extern uint8_t update_available;
extern bool update_ready;

extern char query[QUERY_MAX];
extern uint8_t view;
extern char status_text[48];
extern char launch[VARNAME_MAX];

void ui_palette(void);
void ui_text(int x, int y, const char *text, uint8_t color);
void ui_clipped(int x, int y, const char *text, uint8_t color, uint8_t columns);
void ui_right(const char *text, int y, uint8_t color);
void ui_center(int center, int y, const char *text, uint8_t color);
void ui_number(char *out, uint32_t value);
void ui_size(char *out, uint32_t bytes);
void ui_row(int y, int height, bool selected);
void ui_tag(int x, int y, const char *text, uint8_t color);
void ui_mark(int x, int y, uint8_t color);
void ui_rule(int x, int y, int w);
void ui_brand(int center, int y);
void ui_panel(int x, int y, int w, int h);
void ui_frame(const char *title, const char *footer);
void ui_overlay(void);
void ui_scroll(uint8_t selected, uint8_t count, uint8_t *top, uint8_t rows);
void ui_scrollbar(uint16_t top, uint16_t visible, uint16_t total);
void ui_empty(const char *first, const char *second);
void ui_bar(int x, int y, int w, int h, uint32_t done, uint32_t total);
uint8_t ui_wrap(const char *text, uint16_t *starts, uint8_t *lengths,
                uint8_t max_rows, uint8_t columns);

void store_load(void);
void store_save(void);

install_t *store_find(const char *slug);
void store_record(const program_t *program);
void store_forget_slug(const char *slug);
void store_prune(void);
bool store_installed(const install_t *entry);

uint8_t var_type_from(const json_t *object);

void api_list(void);
void api_page(int8_t delta);
void api_catalog(void);
void api_detail(const char *slug);
void api_updates(void);

void install_begin(const program_t *program);
bool install_have(const install_t *entry);
void install_reset(void);
void install_pump(void);
bool install_busy(void);
uint8_t install_step(void);
uint8_t install_steps(void);
const char *install_label(void);

bool var_exists(const char *name, uint8_t var_type);
bool var_delete(const char *name, uint8_t var_type);
uint32_t var_size(const char *name, uint8_t var_type);
void programs_mark_installed(void);

void notify(const char *text);
const char *footer_text(const char *fallback);
const char *server_url(void);
const char *language_name(uint8_t language);
uint8_t language_color(uint8_t language);
const char *sort_key(uint8_t sort);
const char *sort_name(uint8_t sort);
const char *state_name(uint8_t state);
uint8_t installed_state(const program_t *program);
const char *installed_version(const char *slug);
bool can_run(uint8_t var_type);
void request_run(const char *var_name);
void updates_begin(void);
void updates_next(void);
bool updates_pending(void);
void install_from_slug(const char *slug);

#endif
