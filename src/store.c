#include "swoop.h"

#include <fileioc.h>
#include <string.h>

store_t store;
bool store_dirty;

static void store_scrub(void)
{
    uint8_t handle = ti_Open(STORE_NAME, "r+");
    uint16_t size;

    if (handle == 0)
    {
        return;
    }

    if (ti_SetArchiveStatus(false, handle))
    {
        handle = ti_Open(STORE_NAME, "r+");

        if (handle == 0)
        {
            return;
        }
    }

    size = (uint16_t)ti_GetSize(handle);
    ti_Rewind(handle);

    while (size > 0)
    {
        uint8_t zeros[32];
        uint16_t span = size > sizeof zeros ? (uint16_t)sizeof zeros : size;

        memset(zeros, 0, sizeof zeros);

        if (ti_Write(zeros, span, 1, handle) != 1)
        {
            break;
        }

        size = (uint16_t)(size - span);
    }

    ti_Close(handle);
    ti_Delete(STORE_NAME);
}

void store_load(void)
{
    uint8_t handle;

    memset(&store, 0, sizeof store);

    handle = ti_Open(STORE_NAME, "r");

    if (handle != 0)
    {
        ti_Read(&store, sizeof store, 1, handle);
        ti_Close(handle);
    }

    if (store.version != STORE_VERSION)
    {
        store_scrub();
        memset(&store, 0, sizeof store);
        store.version = STORE_VERSION;
    }

    store.category[CATEGORY_MAX - 1] = 0;

    if (store.language > LANG_ASM)
    {
        store.language = LANG_ANY;
    }

    if (store.sort >= SORT_COUNT)
    {
        store.sort = SORT_NEWEST;
    }

    if (store.install_count > INSTALL_MAX)
    {
        store.install_count = 0;
    }
}

install_t *store_find(const char *slug)
{
    uint8_t index;

    if (slug == NULL || slug[0] == 0)
    {
        return NULL;
    }

    for (index = 0; index < store.install_count; index++)
    {
        if (strcmp(store.installs[index].slug, slug) == 0)
        {
            return &store.installs[index];
        }
    }
    return NULL;
}

void store_record(const program_t *program)
{
    install_t *entry;

    if (program == NULL || program->slug[0] == 0)
    {
        return;
    }

    entry = store_find(program->slug);

    if (entry == NULL)
    {
        if (store.install_count >= INSTALL_MAX)
        {
            memmove(&store.installs[0], &store.installs[1],
                    sizeof store.installs[0] * (INSTALL_MAX - 1));
            store.install_count = INSTALL_MAX - 1;
        }

        entry = &store.installs[store.install_count];
        store.install_count++;
    }

    memset(entry, 0, sizeof *entry);

    strncpy(entry->slug, program->slug, SLUG_MAX - 1);
    strncpy(entry->title, program->title, TITLE_MAX - 1);
    strncpy(entry->var_name, program->var_name, VARNAME_MAX - 1);
    strncpy(entry->version, program->version, VERSION_TEXT - 1);

    strncpy(entry->digest, program->digest, DIGEST_TEXT - 1);

    entry->size = program->size;
    entry->var_type = program->var_type;
    entry->archived = program->archived;

    store_dirty = true;
    store_save();
}

void store_forget_slug(const char *slug)
{
    install_t *entry = store_find(slug);
    uint8_t index;

    if (entry == NULL)
    {
        return;
    }

    index = (uint8_t)(entry - store.installs);

    if (index + 1 < store.install_count)
    {
        memmove(&store.installs[index], &store.installs[index + 1],
                sizeof store.installs[0] * (store.install_count - index - 1));
    }

    store.install_count--;
    memset(&store.installs[store.install_count], 0, sizeof store.installs[0]);

    store_dirty = true;
    store_save();
}

bool store_installed(const install_t *entry)
{
    return entry != NULL && var_exists(entry->var_name, entry->var_type);
}

void store_prune(void)
{
    uint8_t index = 0;
    bool changed = false;

    while (index < store.install_count)
    {
        if (var_exists(store.installs[index].var_name, store.installs[index].var_type))
        {
            index++;
            continue;
        }

        if (index + 1 < store.install_count)
        {
            memmove(&store.installs[index], &store.installs[index + 1],
                    sizeof store.installs[0] * (store.install_count - index - 1));
        }

        store.install_count--;
        memset(&store.installs[store.install_count], 0, sizeof store.installs[0]);
        changed = true;
    }

    if (changed)
    {
        store_dirty = true;
        store_save();
    }
}

void store_save(void)
{
    uint8_t handle;

    store.version = STORE_VERSION;

    handle = ti_Open(STORE_NAME, "w");

    if (handle == 0)
    {
        return;
    }

    ti_Write(&store, sizeof store, 1, handle);
    ti_SetArchiveStatus(true, handle);
    ti_Close(handle);

    store_dirty = false;
}
