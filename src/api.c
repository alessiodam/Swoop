#include "swoop.h"

#include <string.h>

#define PATH_MAX 200

program_t programs[PROGRAM_MAX];
uint8_t program_count;
uint8_t program_index;
uint16_t program_total;
uint16_t program_page;
uint16_t program_pages;

catalog_t catalog[CATALOG_MAX];
uint8_t catalog_count;

program_t detail;
char detail_text[DESC_MAX];
bool detail_ready;

install_t requirements[REQUIRE_MAX];
uint8_t require_count;
bool require_overflow;

char status_text[48];

static char path[PATH_MAX];

static void fail(const char *text)
{
    strncpy(status_text, text, sizeof status_text - 1);
    status_text[sizeof status_text - 1] = 0;
}

static void reported(const char *body, size_t length, uint16_t status)
{
    json_t root;
    char message[48];

    if (json_value(body, body + length, &root) && root.kind == JSON_OBJECT &&
        json_text_member(&root, "message", message, sizeof message) && message[0] != 0)
    {
        fail(message);
        return;
    }

    if (status != 0)
    {
        char text[12];

        ui_number(text, status);
        strcpy(message, "server said ");
        strcat(message, text);
        fail(message);
        return;
    }

    fail(net_error()[0] != 0 ? net_error() : "the server could not be reached");
}

static uint8_t language_of(const json_t *object)
{
    char text[10];

    if (!json_text_member(object, "language", text, sizeof text))
    {
        return LANG_BASIC;
    }
    if (strcmp(text, "asm") == 0)
    {
        return LANG_ASM;
    }
    if (strcmp(text, "lib") == 0)
    {
        return LANG_LIB;
    }
    return LANG_BASIC;
}

uint8_t var_type_from(const json_t *object)
{
    char text[12];

    if (!json_text_member(object, "var_type", text, sizeof text))
    {
        return VAR_PROGRAM;
    }
    if (strcmp(text, "appvar") == 0)
    {
        return VAR_APPVAR;
    }
    if (strcmp(text, "protected") == 0)
    {
        return VAR_PROTECTED;
    }
    return VAR_PROGRAM;
}

static void read_program(const json_t *object, program_t *out)
{
    json_t names;
    json_t first;

    memset(out, 0, sizeof *out);

    json_text_member(object, "slug", out->slug, SLUG_MAX);
    json_text_member(object, "title", out->title, TITLE_MAX);
    json_text_member(object, "summary", out->summary, SUMMARY_MAX);
    json_text_member(object, "author", out->author, AUTHOR_MAX);
    json_text_member(object, "var_name", out->var_name, VARNAME_MAX);
    json_text_member(object, "version", out->version, VERSION_TEXT);
    json_text_member(object, "checksum", out->digest, DIGEST_TEXT);

    out->size = (uint32_t)json_number_member(object, "size", 0);
    out->downloads = (uint32_t)json_number_member(object, "downloads", 0);
    out->language = language_of(object);
    out->var_type = var_type_from(object);
    out->archived = json_flag(object, "archived");

    if (json_member(object, "category_names", &names) && names.kind == JSON_ARRAY &&
        json_at(&names, 0, &first) && first.kind == JSON_STRING)
    {
        json_text(&first, out->category, CATEGORY_MAX);
    }
}

static void on_list(uint16_t status, const char *body, size_t length)
{
    json_t root;
    json_t list;
    uint16_t count;
    uint16_t index;

    program_count = 0;

    if (status != 200 || !json_value(body, body + length, &root) || root.kind != JSON_OBJECT)
    {
        reported(body, length, status);
        return;
    }

    if (!json_member(&root, "programs", &list) || list.kind != JSON_ARRAY)
    {
        fail("the server sent no program list");
        return;
    }

    count = json_count(&list);

    for (index = 0; index < count && program_count < PROGRAM_MAX; index++)
    {
        json_t item;

        if (!json_at(&list, index, &item) || item.kind != JSON_OBJECT)
        {
            continue;
        }

        read_program(&item, &programs[program_count]);
        program_count++;
    }

    program_total = (uint16_t)json_number_member(&root, "total", program_count);
    program_page = (uint16_t)json_number_member(&root, "page", 1);
    program_pages = (uint16_t)json_number_member(&root, "pages", 1);

    if (program_index >= program_count)
    {
        program_index = program_count > 0 ? (uint8_t)(program_count - 1) : 0;
    }

    status_text[0] = 0;
    programs_mark_installed();
}

static void build_list_path(void)
{
    char escaped[QUERY_MAX * 3 + 1];
    char number[8];

    strcpy(path, "/api/v1/programs?per_page=");

    ui_number(number, PROGRAM_MAX);
    strcat(path, number);

    strcat(path, "&sort=");
    strcat(path, sort_key(store.sort));

    if (store.language == LANG_BASIC)
    {
        strcat(path, "&lang=basic");
    }
    else if (store.language == LANG_ASM)
    {
        strcat(path, "&lang=asm");
    }
    else if (store.language == LANG_LIB)
    {
        strcat(path, "&lang=lib");
    }

    if (store.category[0] != 0)
    {
        strcat(path, "&category=");
        strncat(path, store.category, CATEGORY_MAX - 1);
    }

    if (query[0] != 0)
    {
        net_escape(escaped, sizeof escaped, query);
        strcat(path, "&q=");
        strncat(path, escaped, PATH_MAX - strlen(path) - 12);
    }

    if (program_page > 1)
    {
        ui_number(number, program_page);
        strcat(path, "&page=");
        strcat(path, number);
    }
}

void api_list(void)
{
    if (net_busy())
    {
        return;
    }

    build_list_path();

    if (!net_get(net_url(server_url(), path), on_list))
    {
        fail("could not start the request");
    }
}

void api_page(int8_t delta)
{
    int32_t target = (int32_t)program_page + delta;

    if (target < 1 || (program_pages > 0 && target > (int32_t)program_pages))
    {
        return;
    }

    program_page = (uint16_t)target;
    program_index = 0;

    api_list();
}

static void on_catalog(uint16_t status, const char *body, size_t length)
{
    json_t root;
    json_t list;
    uint16_t count;
    uint16_t index;

    catalog_count = 0;

    if (status != 200 || !json_value(body, body + length, &root) || root.kind != JSON_OBJECT)
    {
        reported(body, length, status);
        return;
    }

    if (!json_member(&root, "categories", &list) || list.kind != JSON_ARRAY)
    {
        return;
    }

    count = json_count(&list);

    for (index = 0; index < count && catalog_count < CATALOG_MAX; index++)
    {
        json_t item;

        if (!json_at(&list, index, &item) || item.kind != JSON_OBJECT)
        {
            continue;
        }

        memset(&catalog[catalog_count], 0, sizeof catalog[0]);

        json_text_member(&item, "key", catalog[catalog_count].key, CATEGORY_MAX);
        json_text_member(&item, "name", catalog[catalog_count].name,
                         CATEGORY_MAX + 8);
        catalog[catalog_count].count =
            (uint16_t)json_number_member(&item, "count", 0);

        catalog_count++;
    }
}

void api_catalog(void)
{
    if (net_busy())
    {
        return;
    }

    if (!net_get(net_url(server_url(), "/api/v1/categories"), on_catalog))
    {
        fail("could not start the request");
    }
}

static void read_requirements(const json_t *root)
{
    json_t list;
    uint16_t count;
    uint16_t index;

    require_count = 0;
    require_overflow = false;

    if (!json_member(root, "requires", &list) || list.kind != JSON_ARRAY)
    {
        return;
    }

    count = json_count(&list);

    /*
     * The server flattens the graph and may name more libraries than there is
     * room for here. A program sent without one of them cannot run, so the
     * overflow is carried rather than quietly dropped.
     */
    if (count > REQUIRE_MAX)
    {
        require_overflow = true;
    }

    for (index = 0; index < count && require_count < REQUIRE_MAX; index++)
    {
        install_t *entry = &requirements[require_count];
        json_t item;

        if (!json_at(&list, index, &item) || item.kind != JSON_OBJECT)
        {
            continue;
        }

        memset(entry, 0, sizeof *entry);

        json_text_member(&item, "slug", entry->slug, SLUG_MAX);
        json_text_member(&item, "title", entry->title, TITLE_MAX);
        json_text_member(&item, "var_name", entry->var_name, VARNAME_MAX);
        json_text_member(&item, "version", entry->version, VERSION_TEXT);
        json_text_member(&item, "checksum", entry->digest, DIGEST_TEXT);

        entry->size = (uint32_t)json_number_member(&item, "size", 0);
        entry->var_type = var_type_from(&item);
        entry->archived = json_flag(&item, "archived");

        require_count++;
    }
}

static void on_detail(uint16_t status, const char *body, size_t length)
{
    json_t root;

    detail_ready = false;
    detail_text[0] = 0;
    require_count = 0;
    require_overflow = false;

    if (status != 200 || !json_value(body, body + length, &root) || root.kind != JSON_OBJECT)
    {
        reported(body, length, status);
        return;
    }

    read_program(&root, &detail);
    read_requirements(&root);
    json_text_member(&root, "description", detail_text, DESC_MAX);

    if (detail_text[0] == 0)
    {
        strncpy(detail_text, detail.summary, DESC_MAX - 1);
    }

    detail.installed = var_exists(detail.var_name, detail.var_type);
    detail_ready = true;
    status_text[0] = 0;
}

void api_detail(const char *slug)
{
    if (net_busy() || slug == NULL || slug[0] == 0)
    {
        return;
    }

    detail_ready = false;

    strcpy(path, "/api/v1/programs/");
    strncat(path, slug, SLUG_MAX - 1);

    if (!net_get(net_url(server_url(), path), on_detail))
    {
        fail("could not start the request");
    }
}

update_t updates[INSTALL_MAX];
uint8_t update_count;
uint8_t update_index;
uint8_t update_available;
bool update_ready;

static void on_updates(uint16_t status, const char *body, size_t length)
{
    json_t root;
    json_t list;
    uint16_t count;
    uint16_t index;

    if (status != 200 || !json_value(body, body + length, &root) || root.kind != JSON_OBJECT)
    {
        reported(body, length, status);
        return;
    }

    if (!json_member(&root, "programs", &list) || list.kind != JSON_ARRAY)
    {
        fail("the server sent no versions");
        return;
    }

    count = json_count(&list);

    for (index = 0; index < count; index++)
    {
        char slug[SLUG_MAX];
        char version[VERSION_TEXT];
        char digest[DIGEST_TEXT];
        json_t item;
        install_t *entry;
        update_t *row = NULL;
        uint8_t scan;
        uint32_t size;

        if (!json_at(&list, index, &item) || item.kind != JSON_OBJECT)
        {
            continue;
        }

        slug[0] = 0;
        json_text_member(&item, "slug", slug, SLUG_MAX);

        for (scan = 0; scan < update_count; scan++)
        {
            if (strcmp(updates[scan].slug, slug) == 0)
            {
                row = &updates[scan];
                break;
            }
        }

        if (row == NULL)
        {
            continue;
        }

        entry = store_find(slug);

        if (entry == NULL)
        {
            row->state = STATE_GONE;
            continue;
        }

        if (json_flag(&item, "missing"))
        {
            row->state = STATE_GONE;
            continue;
        }

        version[0] = 0;
        digest[0] = 0;

        json_text_member(&item, "version", version, VERSION_TEXT);
        json_text_member(&item, "checksum", digest, DIGEST_TEXT);
        json_text_member(&item, "title", row->title, TITLE_MAX);

        size = (uint32_t)json_number_member(&item, "size", 0);

        strncpy(row->want, version, VERSION_TEXT - 1);
        row->want[VERSION_TEXT - 1] = 0;
        strncpy(row->digest, digest, DIGEST_TEXT - 1);
        row->digest[DIGEST_TEXT - 1] = 0;
        row->size = size;

        json_text_member(&item, "var_name", row->var_name, VARNAME_MAX);
        row->var_type = var_type_from(&item);
        row->archived = json_flag(&item, "archived");

        if (!var_exists(entry->var_name, entry->var_type))
        {
            row->have[0] = 0;
            row->state = STATE_ABSENT;
            continue;
        }

        if (digest[0] != 0 && entry->digest[0] != 0 &&
            strcmp(digest, entry->digest) != 0)
        {
            row->state = STATE_UPDATE;
            continue;
        }

        if (strcmp(version, entry->version) != 0)
        {
            row->state = STATE_UPDATE;
            continue;
        }

        row->state = STATE_CURRENT;
    }

    update_available = 0;

    for (index = 0; index < update_count; index++)
    {
        if (updates[index].state == STATE_UPDATE || updates[index].state == STATE_ABSENT)
        {
            update_available++;
        }
    }

    update_ready = true;
    status_text[0] = 0;
}

void api_updates(void)
{
    uint8_t index;

    if (net_busy())
    {
        return;
    }

    update_count = 0;
    update_available = 0;
    update_ready = false;

    store_prune();

    strcpy(path, "/api/v1/programs/versions?slugs=");

    for (index = 0; index < store.install_count && update_count < INSTALL_MAX; index++)
    {
        const install_t *entry = &store.installs[index];

        if (strlen(path) + strlen(entry->slug) + 2 >= PATH_MAX)
        {
            break;
        }

        if (update_count > 0)
        {
            strcat(path, ",");
        }
        strcat(path, entry->slug);

        memset(&updates[update_count], 0, sizeof updates[0]);

        strncpy(updates[update_count].slug, entry->slug, SLUG_MAX - 1);
        strncpy(updates[update_count].title, entry->title, TITLE_MAX - 1);
        strncpy(updates[update_count].have, entry->version, VERSION_TEXT - 1);

        updates[update_count].state = STATE_UNKNOWN;
        update_count++;
    }

    if (update_count == 0)
    {
        update_ready = true;
        return;
    }

    if (!net_get(net_url(server_url(), path), on_updates))
    {
        fail("could not start the request");
    }
}
