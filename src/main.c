#include "swoop.h"
#include "input.h"

#include <fileioc.h>
#include <graphx.h>
#include <string.h>
#include <ti/vars.h>
#include <tice.h>

#define MENU_COUNT 7
#define FILTER_ROWS 3
#define INSTALLED_MAX 24

uint8_t view;
char query[QUERY_MAX];
char launch[VARNAME_MAX];

static entry_t entry;
static char message[44];
static uint32_t message_until;

static uint8_t list_top;
static uint8_t menu_index;
static uint8_t filter_row;
static uint8_t filter_index;

static uint16_t detail_starts[DESC_ROWS];
static uint8_t detail_lengths[DESC_ROWS];
static uint8_t detail_rows;
static uint8_t detail_top;
static uint8_t plan_top;

static program_t installed[INSTALLED_MAX];
static uint8_t installed_count;
static uint8_t installed_index;

static bool running;
static bool joining;
static uint32_t join_started;
static bool join_attempted;
static bool join_failed;
static bool handoff_off;
static uint32_t handoff_at;

static const char *const menu_labels[MENU_COUNT] = {
    "Refresh",
    "Search",
    "Filter and sort",
    "Programs on this calculator",
    "Check for updates",
    "About",
    "Quit"
};

void notify(const char *text)
{
    strncpy(message, text, sizeof message - 1);
    message[sizeof message - 1] = 0;
    message_until = witi_ms() + 2500u;
}

const char *footer_text(const char *fallback)
{
    if (message[0] != 0 && (int32_t)(witi_ms() - message_until) < 0)
    {
        return message;
    }

    message[0] = 0;

    return fallback;
}

const char *server_url(void)
{
    return ROOST_URL;
}

/*
 * .lang.basic is the ok colour on the sites, .lang.asm is the accent and
 * .lang.lib is the warn one.
 */
uint8_t language_color(uint8_t language)
{
    switch (language)
    {
        case LANG_ASM:
            return COLOR_ACCENT;

        case LANG_LIB:
            return COLOR_WARN;

        default:
            return COLOR_GOOD;
    }
}

const char *language_name(uint8_t language)
{
    switch (language)
    {
        case LANG_BASIC:
            return "TI-BASIC";

        case LANG_ASM:
            return "ASM";

        case LANG_LIB:
            return "Library";

        default:
            return "any";
    }
}

/* The list has one right-hand column, so the label there is the short one. */
static const char *language_tag(uint8_t language)
{
    switch (language)
    {
        case LANG_ASM:
            return "ASM";

        case LANG_LIB:
            return "LIB";

        default:
            return "BASIC";
    }
}

const char *sort_key(uint8_t sort)
{
    switch (sort)
    {
        case SORT_DOWNLOADS:
            return "downloads";

        case SORT_TITLE:
            return "title";

        case SORT_SIZE:
            return "size";

        case SORT_UPDATED:
            return "updated";

        default:
            return "newest";
    }
}

const char *state_name(uint8_t state)
{
    switch (state)
    {
        case STATE_CURRENT:
            return "up to date";

        case STATE_UPDATE:
            return "update ready";

        case STATE_GONE:
            return "gone from the repo";

        case STATE_ABSENT:
            return "not on this calc";

        case STATE_CHANGED:
            return "changed here";

        default:
            return "checking...";
    }
}

uint8_t installed_state(const program_t *program)
{
    const install_t *entry;

    if (program == NULL || program->slug[0] == 0)
    {
        return STATE_UNKNOWN;
    }

    entry = store_find(program->slug);

    if (entry == NULL)
    {
        return STATE_UNKNOWN;
    }

    if (!var_exists(entry->var_name, entry->var_type))
    {
        return STATE_ABSENT;
    }

    if (program->digest[0] != 0 && entry->digest[0] != 0 &&
        strcmp(program->digest, entry->digest) != 0)
    {
        return STATE_UPDATE;
    }

    if (program->version[0] != 0 && strcmp(program->version, entry->version) != 0)
    {
        return STATE_UPDATE;
    }

    if (entry->size != 0 && var_size(entry->var_name, entry->var_type) != entry->size)
    {
        return STATE_CHANGED;
    }

    return STATE_CURRENT;
}

const char *installed_version(const char *slug)
{
    const install_t *entry = store_find(slug);

    return store_installed(entry) ? entry->version : "";
}

bool can_run(uint8_t var_type)
{
    return var_type == VAR_PROGRAM || var_type == VAR_PROTECTED;
}

void request_run(const char *var_name)
{
    if (var_name == NULL || var_name[0] == 0)
    {
        return;
    }

    strncpy(launch, var_name, VARNAME_MAX - 1);
    launch[VARNAME_MAX - 1] = 0;

    running = false;
}

const char *sort_name(uint8_t sort)
{
    switch (sort)
    {
        case SORT_DOWNLOADS:
            return "most downloaded";

        case SORT_TITLE:
            return "title A to Z";

        case SORT_SIZE:
            return "smallest first";

        case SORT_UPDATED:
            return "recently updated";

        default:
            return "newest first";
    }
}

static bool config_present(void)
{
    uint8_t handle = ti_OpenVar(WITICFG_NAME, "r", OS_TYPE_PRGM);

    if (handle == 0)
    {
        handle = ti_OpenVar(WITICFG_NAME, "r", OS_TYPE_PROT_PRGM);
    }

    if (handle == 0)
    {
        return false;
    }

    ti_Close(handle);
    return true;
}

static void launch_config(void)
{
    net_cancel();
    install_reset();
    net_release();
    witi_end();
    gfx_End();

    if (os_RunPrgm(WITICFG_NAME, NULL, 0, NULL) < 0)
    {
        gfx_Begin();
        gfx_SetDrawBuffer();
        gfx_SetMonospaceFont(COL_W);
        gfx_SetTextConfig(gfx_text_noclip);
        ui_palette();
        witi_begin();
        handoff_at = 0;
        join_failed = true;
        return;
    }

    running = false;
}

static void join_begin(void)
{
    join_attempted = true;
    join_failed = false;
    handoff_off = false;
    handoff_at = 0;

    if (!witi_wifi_auto_connect(NULL, NULL))
    {
        join_failed = true;
        return;
    }

    joining = true;
    join_started = witi_ms();
}

static void begin_entry(char *buffer, uint16_t capacity, const char *initial, uint8_t next)
{
    entry_begin(&entry, buffer, capacity, initial);
    view = next;
}

static void reload(void)
{
    program_page = 1;
    program_index = 0;
    list_top = 0;

    api_list();
}

static void open_detail(void)
{
    if (program_index >= program_count)
    {
        return;
    }

    detail = programs[program_index];
    detail_ready = false;
    detail_rows = 0;
    detail_top = 0;

    view = VIEW_DETAIL;

    api_detail(detail.slug);
}

static void scan_installed(void)
{
    uint8_t index;

    installed_count = 0;
    installed_index = 0;

    for (index = 0; index < program_count && installed_count < INSTALLED_MAX; index++)
    {
        if (!var_exists(programs[index].var_name, programs[index].var_type))
        {
            continue;
        }

        installed[installed_count] = programs[index];
        installed_count++;
    }
}

static void network_pump(void)
{
    if (!witi_ready())
    {
        join_attempted = false;
        join_failed = false;
        joining = false;
        handoff_off = false;
        handoff_at = 0;
        return;
    }

    if (witi_wifi_online())
    {
        joining = false;
        join_failed = false;
        handoff_at = 0;

        if (program_count == 0 && !net_busy())
        {
            api_catalog();
        }
        return;
    }

    if (!join_attempted)
    {
        join_begin();
        return;
    }

    if (joining && witi_ms() - join_started > JOIN_TIMEOUT_MS)
    {
        joining = false;
        join_failed = true;
    }

    if (!join_failed || !config_present() || handoff_off)
    {
        return;
    }

    if (handoff_at == 0)
    {
        handoff_at = witi_ms() + HANDOFF_DELAY_MS;
        notify("no network, opening WITICFG");
        return;
    }

    if ((int32_t)(witi_ms() - handoff_at) >= 0)
    {
        launch_config();
    }
}

static bool shared_keys(void)
{
    if (key_pressed(kb_KeyMode))
    {
        menu_index = 0;
        view = VIEW_MENU;
        return true;
    }

    if (key_pressed(kb_KeyWindow))
    {
        begin_entry(query, QUERY_MAX, query, VIEW_SEARCH);
        return true;
    }

    if (key_pressed(kb_KeyZoom))
    {
        filter_row = 0;
        filter_index = 0;
        view = VIEW_FILTER;

        if (catalog_count == 0 && !net_busy())
        {
            api_catalog();
        }
        return true;
    }

    return false;
}

static void list_update(void)
{
    if (key_pressed(kb_KeyClear))
    {
        running = false;
        return;
    }

    if (shared_keys())
    {
        return;
    }

    if (key_pressed(kb_KeyTrace))
    {
        api_list();
        return;
    }

    if (key_pressed(kb_KeyGraph))
    {
        api_page(1);
        return;
    }

    if (key_pressed(kb_KeyYequ))
    {
        api_page(-1);
        return;
    }

    if (program_count == 0)
    {
        return;
    }

    if (key_repeat(kb_KeyDown) && program_index + 1 < program_count)
    {
        program_index++;
    }

    if (key_repeat(kb_KeyUp) && program_index > 0)
    {
        program_index--;
    }

    ui_scroll(program_index, program_count, &list_top, LIST_ROWS / 2);

    if (key_pressed(kb_KeyEnter) || key_pressed(kb_Key2nd))
    {
        open_detail();
    }
}

static void filter_line(char *out)
{
    out[0] = 0;

    strcat(out, store.category[0] != 0 ? store.category : "every category");
    strcat(out, ", ");
    strcat(out, language_name(store.language));
    strcat(out, ", ");
    strcat(out, sort_name(store.sort));
}

static void list_draw(void)
{
    uint8_t index;
    uint8_t rows_shown = LIST_ROWS / 2;
    char heading[72];
    char text[16];

    filter_line(heading);

    ui_frame(query[0] != 0 ? query : heading,
             footer_text("enter open  window find  zoom filter"));

    if (program_count == 0)
    {
        if (net_busy())
        {
            ui_empty("loading the repository...", NULL);
        }
        else if (status_text[0] != 0)
        {
            ui_center(SCREEN_W / 2, BODY_TOP + BODY_H / 2 - 12, "could not load", COLOR_BAD);
            ui_center(SCREEN_W / 2, BODY_TOP + BODY_H / 2 + 2, status_text, COLOR_DIM);
            ui_center(SCREEN_W / 2, BODY_TOP + BODY_H / 2 + 20, "press trace to try again",
                      COLOR_DIM);
        }
        else
        {
            ui_empty("nothing matches this filter", "press zoom to change it");
        }
        return;
    }

    for (index = list_top; index < program_count && index < list_top + rows_shown; index++)
    {
        const program_t *item = &programs[index];
        int y = BODY_TOP + (index - list_top) * (ROW_H * 2);
        bool chosen = index == program_index;

        ui_row(y, ROW_H * 2, chosen);

        ui_clipped(8, y + 1, item->title, COLOR_TEXT, 28);
        ui_clipped(8, y + 12, item->summary[0] != 0 ? item->summary : item->category,
                   COLOR_DIM, 30);

        ui_right(language_tag(item->language), y + 1, language_color(item->language));

        ui_size(text, item->size);
        ui_right(text, y + 12, COLOR_DIM);

        if (item->installed)
        {
            uint8_t state = installed_state(item);

            ui_mark(SCREEN_W - 76, y + 6,
                    state == STATE_UPDATE || state == STATE_CHANGED ? COLOR_WARN : COLOR_GOOD);
        }
    }

    ui_scrollbar(list_top, rows_shown, program_count);

    if (program_pages > 1)
    {
        char label[24];
        char part[8];

        strcpy(label, "page ");
        ui_number(part, program_page);
        strcat(label, part);
        strcat(label, " of ");
        ui_number(part, program_pages);
        strcat(label, part);

        ui_right(label, 3, COLOR_DIM);
    }
}

static void detail_layout(void)
{
    if (detail_rows != 0 || !detail_ready)
    {
        return;
    }

    detail_rows = ui_wrap(detail_text, detail_starts, detail_lengths, DESC_ROWS, 37);
}

static void detail_update(void)
{
    if (key_pressed(kb_KeyClear))
    {
        view = VIEW_LIST;
        return;
    }

    if (shared_keys())
    {
        return;
    }

    if (!detail_ready)
    {
        return;
    }

    if (key_pressed(kb_KeyEnter) || key_pressed(kb_Key2nd))
    {
        /*
         * Anything with dependencies is written out first, so the reader sees
         * every variable that is about to land before one of them does.
         */
        if (require_count > 0 || require_overflow)
        {
            plan_top = 0;
            view = VIEW_PLAN;
            return;
        }

        install_begin(&detail);
        job.origin = VIEW_DETAIL;
        view = VIEW_INSTALL;
        return;
    }

    if (key_pressed(kb_KeyGraph) && detail.installed && can_run(detail.var_type))
    {
        request_run(detail.var_name);
        return;
    }

    if (key_pressed(kb_KeyDel) && detail.installed)
    {
        if (var_delete(detail.var_name, detail.var_type))
        {
            detail.installed = false;
            store_forget_slug(detail.slug);
            programs_mark_installed();
            notify("deleted from the calculator");
        }
        else
        {
            notify("could not delete it");
        }
        return;
    }

    if (key_repeat(kb_KeyDown) && detail_top + 6 < detail_rows)
    {
        detail_top++;
    }

    if (key_repeat(kb_KeyUp) && detail_top > 0)
    {
        detail_top--;
    }
}

static void detail_draw(void)
{
    char text[24];
    int y;
    uint8_t index;
    uint8_t visible;

    ui_frame(detail.title[0] != 0 ? detail.title : "Program",
             footer_text(require_count > 0 || require_overflow
                             ? "enter what it needs   clear back"
                             : installed_state(&detail) == STATE_UPDATE
                                   ? "enter update   graph run   del remove"
                                   : detail.installed
                                         ? "enter reinstall   graph run   del remove"
                                         : "enter send to calculator   clear back"));

    if (!detail_ready)
    {
        if (status_text[0] != 0 && !net_busy())
        {
            ui_center(SCREEN_W / 2, BODY_TOP + BODY_H / 2 - 12, "could not load", COLOR_BAD);
            ui_center(SCREEN_W / 2, BODY_TOP + BODY_H / 2 + 2, status_text, COLOR_DIM);
        }
        else
        {
            ui_empty("loading...", NULL);
        }
        return;
    }

    y = BODY_TOP;

    ui_tag(6, y, language_name(detail.language), language_color(detail.language));

    {
        uint8_t state = installed_state(&detail);

        if (state == STATE_UPDATE)
        {
            ui_tag(90, y, "update ready", COLOR_WARN);
        }
        else if (state == STATE_CHANGED)
        {
            ui_tag(90, y, "changed here", COLOR_WARN);
        }
        else if (state == STATE_CURRENT)
        {
            ui_tag(90, y, "up to date", COLOR_GOOD);
        }
        else if (detail.installed)
        {
            ui_tag(90, y, "on this calc", COLOR_GOOD);
        }
    }

    y += 16;

    ui_text(6, y, "name", COLOR_DIM);
    ui_clipped(80, y, detail.var_name, COLOR_TEXT, 12);

    ui_size(text, detail.size);
    ui_right(text, y, COLOR_TEXT);

    y += LINE_H + 2;

    ui_text(6, y, "category", COLOR_DIM);
    ui_clipped(80, y, detail.category, COLOR_TEXT, 20);

    ui_number(text, detail.downloads);
    strcat(text, " dl");
    ui_right(text, y, COLOR_DIM);

    y += LINE_H + 2;

    if (detail.version[0] != 0)
    {
        const char *have = installed_version(detail.slug);

        ui_text(6, y, "version", COLOR_DIM);
        ui_clipped(80, y, detail.version, COLOR_TEXT, 16);

        if (have[0] != 0 && strcmp(have, detail.version) != 0)
        {
            strcpy(text, "you have ");
            strncat(text, have, sizeof text - strlen(text) - 1);
            ui_right(text, y, COLOR_WARN);
        }

        y += LINE_H + 2;
    }

    if (detail.author[0] != 0)
    {
        ui_text(6, y, "author", COLOR_DIM);
        ui_clipped(80, y, detail.author, COLOR_TEXT, 24);
        y += LINE_H + 2;
    }

    if (require_count > 0)
    {
        char needs[COLS + 1];

        needs[0] = 0;

        for (index = 0; index < require_count; index++)
        {
            if (index > 0)
            {
                strncat(needs, ", ", sizeof needs - strlen(needs) - 1);
            }
            strncat(needs, requirements[index].title[0] != 0 ? requirements[index].title
                                                             : requirements[index].slug,
                    sizeof needs - strlen(needs) - 1);
        }

        ui_text(6, y, "needs", COLOR_DIM);
        ui_clipped(80, y, needs, COLOR_WARN, 28);
        y += LINE_H + 2;
    }

    gfx_SetColor(COLOR_RULE);
    gfx_HorizLine(6, y + 2, SCREEN_W - 12);

    y += 8;

    detail_layout();

    visible = (uint8_t)((BODY_BOTTOM - y) / LINE_H);

    for (index = detail_top; index < detail_rows && index < detail_top + visible; index++)
    {
        char line[COLS + 1];
        uint8_t length = detail_lengths[index];

        if (length > COLS)
        {
            length = COLS;
        }

        memcpy(line, detail_text + detail_starts[index], length);
        line[length] = 0;

        ui_text(6, y + (index - detail_top) * LINE_H, line, COLOR_TEXT);
    }

    if (detail_rows > visible)
    {
        ui_scrollbar(detail_top, visible, detail_rows);
    }
}

/*
 * The plan is the queue the install runs: every library the server flattened,
 * in its order, and the program the reader picked last.
 */
static uint8_t plan_rows(void)
{
    return (uint8_t)(require_count + 1);
}

static uint8_t plan_visible(void)
{
    return (uint8_t)((BODY_BOTTOM - (BODY_TOP + 16) - 12) / ROW_H);
}

static const install_t *plan_entry(uint8_t index, install_t *scratch)
{
    if (index < require_count)
    {
        return &requirements[index];
    }

    memset(scratch, 0, sizeof *scratch);

    strncpy(scratch->slug, detail.slug, SLUG_MAX - 1);
    strncpy(scratch->title, detail.title, TITLE_MAX - 1);
    strncpy(scratch->var_name, detail.var_name, VARNAME_MAX - 1);
    strncpy(scratch->version, detail.version, VERSION_TEXT - 1);
    strncpy(scratch->digest, detail.digest, DIGEST_TEXT - 1);

    scratch->size = detail.size;
    scratch->var_type = detail.var_type;
    scratch->archived = detail.archived;

    return scratch;
}

static void plan_update(void)
{
    uint8_t rows = plan_rows();
    uint8_t visible = plan_visible();

    if (key_pressed(kb_KeyClear))
    {
        view = VIEW_DETAIL;
        return;
    }

    if (key_pressed(kb_KeyEnter) || key_pressed(kb_Key2nd))
    {
        if (require_overflow)
        {
            notify("it needs more libraries than this app can send");
            return;
        }

        install_begin(&detail);
        job.origin = VIEW_DETAIL;
        view = VIEW_INSTALL;
        return;
    }

    if (key_repeat(kb_KeyDown) && plan_top + visible < rows)
    {
        plan_top++;
    }

    if (key_repeat(kb_KeyUp) && plan_top > 0)
    {
        plan_top--;
    }
}

static void plan_draw(void)
{
    install_t scratch;
    char text[32];
    uint8_t rows = plan_rows();
    uint8_t visible = plan_visible();
    uint8_t index;
    uint8_t pending = 0;
    uint32_t total = 0;

    ui_frame("What lands on the calculator",
             footer_text(require_overflow ? "clear back"
                                          : "enter send it all   clear back"));

    for (index = 0; index < rows; index++)
    {
        const install_t *entry = plan_entry(index, &scratch);

        if (index + 1 == rows || !install_have(entry))
        {
            total += entry->size;
            pending++;
        }
    }

    ui_number(text, require_count);
    strcat(text, require_count == 1 ? " library, then the program"
                                    : " libraries, then the program");
    ui_clipped(6, BODY_TOP, text, COLOR_DIM, 30);

    ui_size(text, total);
    ui_right(text, BODY_TOP, COLOR_ACCENT);

    ui_rule(6, BODY_TOP + 11, SCREEN_W - 12);

    for (index = plan_top; index < rows && index < plan_top + visible; index++)
    {
        const install_t *entry = plan_entry(index, &scratch);
        bool last = (uint8_t)(index + 1) == rows;
        bool here = !last && install_have(entry);
        int y = BODY_TOP + 16 + (index - plan_top) * ROW_H;

        ui_row(y, ROW_H, last);
        ui_clipped(8, y + 1, entry->title[0] != 0 ? entry->title : entry->slug,
                   COLOR_TEXT, 20);
        ui_clipped(178, y + 1, entry->var_name, COLOR_FAINT, 8);

        if (here)
        {
            ui_right("already here", y + 1, COLOR_GOOD);
        }
        else
        {
            ui_size(text, entry->size);
            ui_right(text, y + 1, COLOR_DIM);
        }
    }

    if (rows > visible)
    {
        ui_scrollbar(plan_top, visible, rows);
    }

    if (require_overflow)
    {
        ui_text(6, BODY_BOTTOM - 9, "more libraries than this app can send", COLOR_BAD);
        return;
    }

    ui_number(text, pending);
    strcat(text, " to download");
    ui_text(6, BODY_BOTTOM - 9, text, COLOR_DIM);
}

static void install_view_update(void)
{
    if (install_busy())
    {
        return;
    }

    if (job.state == JOB_DONE && !job.settled)
    {
        job.settled = true;

        if (job.origin == VIEW_UPDATES && update_index < update_count)
        {
            updates[update_index].state = STATE_CURRENT;
            strncpy(updates[update_index].have, updates[update_index].want,
                    VERSION_TEXT - 1);
            updates[update_index].have[VERSION_TEXT - 1] = 0;

            if (update_available > 0)
            {
                update_available--;
            }
        }

        if (job.origin == VIEW_UPDATES && updates_pending())
        {
            install_reset();
            view = VIEW_UPDATES;
            updates_next();
            return;
        }
    }

    if (key_pressed(kb_KeyGraph) && job.state == JOB_DONE && can_run(job.var_type))
    {
        request_run(job.var_name);
        return;
    }

    if (key_pressed(kb_KeyClear) || key_pressed(kb_KeyEnter) || key_pressed(kb_Key2nd))
    {
        uint8_t back = job.origin == VIEW_UPDATES ? VIEW_UPDATES : VIEW_DETAIL;

        if (job.state == JOB_DONE)
        {
            notify(job.message);
        }

        install_reset();
        view = back;
    }
}

static void install_view_draw(void)
{
    char text[24];
    uint32_t total = job.expected;

    ui_frame("Sending",
             install_busy()
                 ? "please wait"
                 : (job.state == JOB_DONE && can_run(job.var_type) ? "graph run   enter done"
                                                                  : "enter done"));

    ui_center(SCREEN_W / 2, BODY_TOP + 20, install_label(), COLOR_TEXT);
    ui_center(SCREEN_W / 2, BODY_TOP + 36, job.var_name, COLOR_DIM);

    if (job.queue_count > 0 && job.state != JOB_FAILED)
    {
        char step[24];

        ui_number(step, install_step());
        strcat(step, " of ");
        ui_number(step + strlen(step), install_steps());

        ui_center(SCREEN_W / 2, BODY_TOP + 52,
                  job.state == JOB_DONE ? "with everything it needs" : step,
                  job.state == JOB_DONE ? COLOR_GOOD : COLOR_ACCENT);
    }

    if (job.state == JOB_FAILED)
    {
        ui_center(SCREEN_W / 2, BODY_TOP + 76, "it did not arrive", COLOR_BAD);
        ui_center(SCREEN_W / 2, BODY_TOP + 94, job.message, COLOR_DIM);
        ui_center(SCREEN_W / 2, BODY_TOP + 124, "press enter to go back", COLOR_DIM);
        return;
    }

    if (job.state == JOB_DONE)
    {
        ui_center(SCREEN_W / 2, BODY_TOP + 76, "done", COLOR_GOOD);

        ui_size(text, job.written);
        ui_center(SCREEN_W / 2, BODY_TOP + 94, text, COLOR_DIM);

        ui_center(SCREEN_W / 2, BODY_TOP + 112,
                  job.archived ? "archived on the calculator" : "left in RAM", COLOR_DIM);

        if (can_run(job.var_type))
        {
            ui_center(SCREEN_W / 2, BODY_TOP + 138, "press graph to run it now", COLOR_ACCENT);
            ui_center(SCREEN_W / 2, BODY_TOP + 152, "or enter to stay here", COLOR_DIM);
        }
        else
        {
            ui_center(SCREEN_W / 2, BODY_TOP + 140, "press enter to go back", COLOR_DIM);
        }
        return;
    }

    if (total == 0)
    {
        total = net_expected();
    }

    if (total == HTTP_LENGTH_UNKNOWN)
    {
        total = 0;
    }

    ui_bar(40, BODY_TOP + 76, SCREEN_W - 80, 12, job.written, total);

    ui_size(text, job.written);
    ui_center(SCREEN_W / 2, BODY_TOP + 98, text, COLOR_DIM);

    ui_center(SCREEN_W / 2, BODY_TOP + 124,
              job.state == JOB_WRITING ? "writing to the calculator" : job.message,
              COLOR_DIM);
}

static void search_update(void)
{
    if (entry_update(&entry))
    {
        return;
    }

    if (key_pressed(kb_KeyClear))
    {
        view = VIEW_LIST;
        return;
    }

    if (key_pressed(kb_KeyEnter))
    {
        view = VIEW_LIST;
        reload();
    }
}

static void search_draw(void)
{
    ui_frame("Find a program", "2nd symbols   alpha case   enter search");

    entry_draw_field(&entry, 4, BODY_TOP + 20, SCREEN_W - 8, false);

    ui_text(6, BODY_TOP + 46, "matches titles, summaries and tags", COLOR_DIM);
    ui_text(6, BODY_TOP + 58, "leave it empty to clear the search", COLOR_DIM);

    ui_right(entry_mode_name(&entry), SCREEN_H - FOOTER_H + 2, COLOR_ACCENT);

    entry_draw_overlay(&entry);
}

static uint8_t filter_options(uint8_t row)
{
    switch (row)
    {
        case 0:
            return (uint8_t)(catalog_count + 1);

        case 1:
            return LANG_COUNT;

        default:
            return SORT_COUNT;
    }
}

static void filter_apply(void)
{
    switch (filter_row)
    {
        case 0:
            if (filter_index == 0)
            {
                store.category[0] = 0;
            }
            else
            {
                strncpy(store.category, catalog[filter_index - 1].key, CATEGORY_MAX - 1);
                store.category[CATEGORY_MAX - 1] = 0;
            }
            break;

        case 1:
            store.language = filter_index;
            break;

        default:
            store.sort = filter_index;
            break;
    }

    store_dirty = true;
    store_save();

    view = VIEW_LIST;
    reload();
}

static void filter_sync(void)
{
    uint8_t index;

    filter_index = 0;

    if (filter_row == 0)
    {
        if (store.category[0] == 0)
        {
            return;
        }
        for (index = 0; index < catalog_count; index++)
        {
            if (strcmp(catalog[index].key, store.category) == 0)
            {
                filter_index = (uint8_t)(index + 1);
                return;
            }
        }
        return;
    }

    filter_index = filter_row == 1 ? store.language : store.sort;
}

static void filter_update(void)
{
    uint8_t count = filter_options(filter_row);

    if (key_pressed(kb_KeyClear))
    {
        view = VIEW_LIST;
        return;
    }

    if (key_pressed(kb_KeyTrace))
    {
        api_catalog();
        return;
    }

    if (key_repeat(kb_KeyRight) && filter_row + 1 < FILTER_ROWS)
    {
        filter_row++;
        filter_sync();
        return;
    }

    if (key_repeat(kb_KeyLeft) && filter_row > 0)
    {
        filter_row--;
        filter_sync();
        return;
    }

    if (count == 0)
    {
        return;
    }

    if (key_repeat(kb_KeyDown) && filter_index + 1 < count)
    {
        filter_index++;
    }

    if (key_repeat(kb_KeyUp) && filter_index > 0)
    {
        filter_index--;
    }

    ui_scroll(filter_index, count, &list_top, LIST_ROWS);

    if (key_pressed(kb_KeyEnter) || key_pressed(kb_Key2nd))
    {
        filter_apply();
    }
}

static void filter_draw(void)
{
    static const char *const rows[FILTER_ROWS] = { "Category", "Language", "Sort" };
    uint8_t count = filter_options(filter_row);
    uint8_t index;

    ui_frame("Filter and sort", "arrows move   enter apply   clear back");

    for (index = 0; index < FILTER_ROWS; index++)
    {
        int x = 6 + index * 104;

        ui_text(x, BODY_TOP, rows[index], index == filter_row ? COLOR_ACCENT : COLOR_DIM);

        if (index == filter_row)
        {
            gfx_SetColor(COLOR_ACCENT_FILL);
            gfx_FillRectangle(x, BODY_TOP + 11, (int)gfx_GetStringWidth(rows[index]), 2);
        }
    }

    if (count == 0)
    {
        ui_empty("no categories yet", "press trace to load them");
        return;
    }

    for (index = list_top; index < count && index < list_top + LIST_ROWS - 2; index++)
    {
        int y = BODY_TOP + 22 + (index - list_top) * ROW_H;
        const char *label = "";
        char suffix[12];

        suffix[0] = 0;

        if (filter_row == 0)
        {
            if (index == 0)
            {
                label = "Every category";
            }
            else
            {
                label = catalog[index - 1].name;
                ui_number(suffix, catalog[index - 1].count);
            }
        }
        else if (filter_row == 1)
        {
            label = index == LANG_ANY ? "Any language"
                                      : language_name(index);
        }
        else
        {
            label = sort_name(index);
        }

        ui_row(y, ROW_H, index == filter_index);
        ui_clipped(10, y + 1, label, COLOR_TEXT, 30);

        if (suffix[0] != 0)
        {
            ui_right(suffix, y + 1, COLOR_DIM);
        }
    }

    ui_scrollbar(list_top, (uint16_t)(LIST_ROWS - 2), count);
}

static void installed_update(void)
{
    if (key_pressed(kb_KeyClear))
    {
        view = VIEW_LIST;
        return;
    }

    if (key_pressed(kb_KeyTrace))
    {
        scan_installed();
        return;
    }

    if (installed_count == 0)
    {
        return;
    }

    if (key_repeat(kb_KeyDown) && installed_index + 1 < installed_count)
    {
        installed_index++;
    }

    if (key_repeat(kb_KeyUp) && installed_index > 0)
    {
        installed_index--;
    }

    ui_scroll(installed_index, installed_count, &list_top, LIST_ROWS);

    if (key_pressed(kb_KeyDel))
    {
        const program_t *item = &installed[installed_index];

        if (var_delete(item->var_name, item->var_type))
        {
            notify("deleted");
            store_forget_slug(item->slug);
            programs_mark_installed();
            scan_installed();
        }
        else
        {
            notify("could not delete it");
        }
    }
}

static void installed_draw(void)
{
    uint8_t index;

    ui_frame("On this calculator", footer_text("del remove   trace rescan   clear back"));

    if (installed_count == 0)
    {
        ui_empty("nothing from the Roost is installed",
                 "only programs in the current list are shown");
        return;
    }

    for (index = list_top; index < installed_count && index < list_top + LIST_ROWS; index++)
    {
        const program_t *item = &installed[index];
        int y = BODY_TOP + (index - list_top) * ROW_H;
        char text[16];

        ui_row(y, ROW_H, index == installed_index);
        ui_clipped(10, y + 1, item->var_name, COLOR_TEXT, 10);
        ui_clipped(96, y + 1, item->title, COLOR_DIM, 18);

        text[0] = 0;

        if (installed_version(item->slug)[0] != 0)
        {
            strncat(text, installed_version(item->slug), sizeof text - 2);
        }
        else
        {
            ui_size(text, item->size);
        }

        ui_right(text, y + 1, COLOR_DIM);
    }

    ui_scrollbar(list_top, LIST_ROWS, installed_count);
}

static void about_draw(void)
{
    ui_frame("About", "clear back");

    ui_brand(SCREEN_W / 2, BODY_TOP + 10);

    ui_center(SCREEN_W / 2, BODY_TOP + 36, SWOOP_RELEASE, COLOR_DIM);

    ui_text(10, BODY_TOP + 62, "The CEagle program repository.", COLOR_TEXT);
    ui_text(10, BODY_TOP + 78, "Programs arrive over Wi-Fi as raw", COLOR_DIM);
    ui_text(10, BODY_TOP + 90, "variable data and are written", COLOR_DIM);
    ui_text(10, BODY_TOP + 102, "straight into a calculator variable.", COLOR_DIM);
    ui_text(10, BODY_TOP + 122, "On a computer the same program is", COLOR_DIM);
    ui_text(10, BODY_TOP + 134, "served as a .8xp file.", COLOR_DIM);

    ui_clipped(10, BODY_TOP + 156, server_url(), COLOR_TEXT, 37);
}

void install_from_slug(const char *slug)
{
    program_t target;
    uint8_t index;

    for (index = 0; index < update_count; index++)
    {
        const update_t *row = &updates[index];

        if (strcmp(row->slug, slug) != 0)
        {
            continue;
        }

        memset(&target, 0, sizeof target);

        strncpy(target.slug, row->slug, SLUG_MAX - 1);
        strncpy(target.title, row->title, TITLE_MAX - 1);
        strncpy(target.var_name, row->var_name, VARNAME_MAX - 1);
        strncpy(target.version, row->want, VERSION_TEXT - 1);
        strncpy(target.digest, row->digest, DIGEST_TEXT - 1);

        target.size = row->size;
        target.var_type = row->var_type;
        target.archived = row->archived;

        update_index = index;

        install_begin(&target);
        job.origin = VIEW_UPDATES;
        view = VIEW_INSTALL;
        return;
    }
}

void updates_begin(void)
{
    update_index = 0;
    list_top = 0;
    view = VIEW_UPDATES;

    api_updates();
}

bool updates_pending(void)
{
    uint8_t index;

    for (index = 0; index < update_count; index++)
    {
        if (updates[index].chosen)
        {
            return true;
        }
    }
    return false;
}

void updates_next(void)
{
    uint8_t index;

    for (index = 0; index < update_count; index++)
    {
        if (updates[index].state != STATE_UPDATE && updates[index].state != STATE_ABSENT)
        {
            continue;
        }
        if (!updates[index].chosen)
        {
            continue;
        }

        updates[index].chosen = false;
        update_index = index;

        install_from_slug(updates[index].slug);
        return;
    }
}

static void updates_update(void)
{
    if (install_busy())
    {
        return;
    }

    if (key_pressed(kb_KeyClear))
    {
        view = VIEW_LIST;
        return;
    }

    if (key_pressed(kb_KeyMode))
    {
        menu_index = 0;
        view = VIEW_MENU;
        return;
    }

    if (key_pressed(kb_KeyTrace))
    {
        api_updates();
        return;
    }

    if (!update_ready || update_count == 0)
    {
        return;
    }

    if (key_repeat(kb_KeyDown) && update_index + 1 < update_count)
    {
        update_index++;
    }

    if (key_repeat(kb_KeyUp) && update_index > 0)
    {
        update_index--;
    }

    ui_scroll(update_index, update_count, &list_top, LIST_ROWS / 2);

    if (key_pressed(kb_KeyGraph) && update_available > 0)
    {
        uint8_t index;

        for (index = 0; index < update_count; index++)
        {
            updates[index].chosen = updates[index].state == STATE_UPDATE ||
                                    updates[index].state == STATE_ABSENT;
        }

        updates_next();
        return;
    }

    if (key_pressed(kb_KeyEnter) || key_pressed(kb_Key2nd))
    {
        const update_t *row = &updates[update_index];

        if (row->state != STATE_UPDATE && row->state != STATE_ABSENT)
        {
            notify("that one is already up to date");
            return;
        }

        install_from_slug(row->slug);
    }
}

static void updates_draw(void)
{
    uint8_t index;
    uint8_t rows_shown = LIST_ROWS / 2;
    char text[24];

    ui_frame("Updates", footer_text("enter install  graph all  trace recheck"));

    if (!update_ready)
    {
        ui_empty("checking the repository...", NULL);
        return;
    }

    if (update_count == 0)
    {
        ui_empty("nothing installed from the Roost yet",
                 "send a program first, then come back");
        return;
    }

    for (index = list_top; index < update_count && index < list_top + rows_shown; index++)
    {
        const update_t *row = &updates[index];
        int y = BODY_TOP + (index - list_top) * (ROW_H * 2);
        uint8_t colour = COLOR_DIM;

        ui_row(y, ROW_H * 2, index == update_index);
        ui_clipped(8, y + 1, row->title[0] != 0 ? row->title : row->slug, COLOR_TEXT, 28);

        if (row->state == STATE_UPDATE || row->state == STATE_ABSENT)
        {
            colour = COLOR_WARN;
        }
        else if (row->state == STATE_CURRENT)
        {
            colour = COLOR_GOOD;
        }
        else if (row->state == STATE_GONE)
        {
            colour = COLOR_BAD;
        }

        ui_right(state_name(row->state), y + 1, colour);

        text[0] = 0;

        if (row->have[0] != 0)
        {
            strcat(text, row->have);
        }

        if (row->state == STATE_UPDATE && row->want[0] != 0 &&
            strcmp(row->have, row->want) != 0)
        {
            strcat(text, " -> ");
            strncat(text, row->want, sizeof text - strlen(text) - 1);
        }

        ui_clipped(8, y + 12, text, COLOR_DIM, 30);
    }

    ui_scrollbar(list_top, rows_shown, update_count);

    if (update_available > 0)
    {
        ui_number(text, update_available);
        strcat(text, " to install");
        ui_right(text, 3, COLOR_WARN);
    }
    else
    {
        ui_right("all up to date", 3, COLOR_GOOD);
    }
}

static void menu_action(uint8_t index)
{
    view = VIEW_LIST;

    switch (index)
    {
        case 0:
            api_list();
            break;

        case 1:
            begin_entry(query, QUERY_MAX, query, VIEW_SEARCH);
            break;

        case 2:
            filter_row = 0;
            list_top = 0;
            filter_sync();
            view = VIEW_FILTER;

            if (catalog_count == 0 && !net_busy())
            {
                api_catalog();
            }
            break;

        case 3:
            scan_installed();
            list_top = 0;
            view = VIEW_INSTALLED;
            break;

        case 4:
            updates_begin();
            break;

        case 5:
            view = VIEW_ABOUT;
            break;

        default:
            running = false;
            break;
    }
}

static void menu_update(void)
{
    if (key_pressed(kb_KeyClear) || key_pressed(kb_KeyMode))
    {
        view = VIEW_LIST;
        return;
    }

    if (key_repeat(kb_KeyDown) && menu_index + 1 < MENU_COUNT)
    {
        menu_index++;
    }

    if (key_repeat(kb_KeyUp) && menu_index > 0)
    {
        menu_index--;
    }

    if (key_pressed(kb_KeyEnter) || key_pressed(kb_Key2nd))
    {
        menu_action(menu_index);
    }
}

static void menu_draw(void)
{
    int x = SCREEN_W - 192;
    int y = BODY_TOP;
    uint8_t index;

    ui_frame("Menu", footer_text("enter choose   clear close"));

    ui_panel(x, y, 188, MENU_COUNT * LINE_H + 8);

    for (index = 0; index < MENU_COUNT; index++)
    {
        int row = y + 4 + index * LINE_H;

        if (index == menu_index)
        {
            gfx_SetColor(COLOR_HOVER);
            gfx_FillRectangle(x + 1, row - 1, 186, LINE_H);

            gfx_SetColor(COLOR_ACCENT_FILL);
            gfx_FillRectangle(x + 1, row - 1, 2, LINE_H);
        }

        ui_clipped(x + 6, row, menu_labels[index], COLOR_TEXT, 22);
    }
}

static void about_update(void)
{
    if (key_pressed(kb_KeyClear) || key_pressed(kb_KeyEnter) || key_pressed(kb_KeyMode))
    {
        view = VIEW_LIST;
    }
}

static void update(void)
{
    switch (view)
    {
        case VIEW_LIST:
            list_update();
            break;

        case VIEW_DETAIL:
            detail_update();
            break;

        case VIEW_SEARCH:
            search_update();
            break;

        case VIEW_FILTER:
            filter_update();
            break;

        case VIEW_PLAN:
            plan_update();
            break;

        case VIEW_INSTALL:
            install_view_update();
            break;

        case VIEW_INSTALLED:
            installed_update();
            break;

        case VIEW_UPDATES:
            updates_update();
            break;

        case VIEW_MENU:
            menu_update();
            break;

        case VIEW_ABOUT:
            about_update();
            break;

        default:
            view = VIEW_LIST;
            break;
    }
}

static void draw(void)
{
    switch (view)
    {
        case VIEW_LIST:
            list_draw();
            break;

        case VIEW_DETAIL:
            detail_draw();
            break;

        case VIEW_SEARCH:
            search_draw();
            break;

        case VIEW_FILTER:
            filter_draw();
            break;

        case VIEW_PLAN:
            plan_draw();
            break;

        case VIEW_INSTALL:
            install_view_draw();
            break;

        case VIEW_INSTALLED:
            installed_draw();
            break;

        case VIEW_UPDATES:
            updates_draw();
            break;

        case VIEW_MENU:
            menu_draw();
            break;

        case VIEW_ABOUT:
            about_draw();
            break;

        default:
            break;
    }

    ui_overlay();

    gfx_SwapDraw();
}

static void background(void)
{
    if (net_busy() || install_busy())
    {
        return;
    }

    if (!witi_wifi_online())
    {
        return;
    }

    if (catalog_count == 0 && status_text[0] == 0)
    {
        api_catalog();
        return;
    }

    if (program_count == 0 && status_text[0] == 0 && view == VIEW_LIST)
    {
        api_list();
    }
}

int main(void)
{
    store_load();
    store_prune();
    keys_init();
    net_init();

    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_SetMonospaceFont(COL_W);
    gfx_SetTextConfig(gfx_text_noclip);
    ui_palette();

    if (!witi_begin())
    {
        gfx_FillScreen(COLOR_BG);
        ui_text(6, 110, "USB could not be opened", COLOR_BAD);
        gfx_SwapDraw();
    }

    running = true;
    view = VIEW_LIST;
    program_page = 1;
    program_pages = 1;

    while (running)
    {
        witi_poll();
        network_pump();
        net_update();
        install_pump();
        keys_poll();

        update();
        background();
        draw();
    }

    net_cancel();
    install_reset();
    net_release();
    witi_end();
    gfx_End();

    if (store_dirty)
    {
        store_save();
    }

    if (launch[0] != 0 && os_RunPrgm(launch, NULL, 0, NULL) < 0)
    {
        os_ClrHome();
        os_PutStrFull("Could not run ");
        os_PutStrFull(launch);
    }

    return 0;
}
