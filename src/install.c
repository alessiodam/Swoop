#include "swoop.h"

#include <fileioc.h>
#include <string.h>

job_t job;

static uint8_t os_type(uint8_t var_type)
{
    switch (var_type)
    {
        case VAR_APPVAR:
            return OS_TYPE_APPVAR;

        case VAR_PROTECTED:
            return OS_TYPE_PROT_PRGM;

        default:
            return OS_TYPE_PRGM;
    }
}

bool var_exists(const char *name, uint8_t var_type)
{
    uint8_t handle;

    if (name == NULL || name[0] == 0)
    {
        return false;
    }

    handle = ti_OpenVar(name, "r", os_type(var_type));

    if (handle == 0)
    {
        return false;
    }

    ti_Close(handle);

    return true;
}

bool var_delete(const char *name, uint8_t var_type)
{
    if (name == NULL || name[0] == 0)
    {
        return false;
    }
    return ti_DeleteVar(name, os_type(var_type)) != 0;
}

uint32_t var_size(const char *name, uint8_t var_type)
{
    uint8_t handle;
    uint16_t size;

    if (name == NULL || name[0] == 0)
    {
        return 0;
    }

    handle = ti_OpenVar(name, "r", os_type(var_type));

    if (handle == 0)
    {
        return 0;
    }

    size = ti_GetSize(handle);
    ti_Close(handle);

    return size;
}

void programs_mark_installed(void)
{
    uint8_t index;

    for (index = 0; index < program_count; index++)
    {
        programs[index].installed =
            var_exists(programs[index].var_name, programs[index].var_type);
    }
}

static void message(const char *text)
{
    strncpy(job.message, text, sizeof job.message - 1);
    job.message[sizeof job.message - 1] = 0;
}

static void discard(void)
{
    if (job.opened)
    {
        ti_Close(job.handle);
        job.opened = false;
    }

    if (job.var_name[0] != 0)
    {
        var_delete(job.var_name, job.var_type);
    }
}

static void fail(const char *text)
{
    discard();

    job.want = WANT_NOTHING;
    job.state = JOB_FAILED;
    message(text);
}

void install_reset(void)
{
    if (job.opened)
    {
        ti_Close(job.handle);
    }

    memset(&job, 0, sizeof job);
}

bool install_busy(void)
{
    return job.state == JOB_RUNNING || job.state == JOB_WRITING ||
           job.want != WANT_NOTHING;
}

uint8_t install_step(void)
{
    return (uint8_t)(job.queue_index + 1);
}

uint8_t install_steps(void)
{
    return (uint8_t)(job.queue_count + 1);
}

const char *install_label(void)
{
    if (job.dependency && job.queue_index < job.queue_count)
    {
        return job.queue[job.queue_index].title[0] != 0
                   ? job.queue[job.queue_index].title
                   : job.queue[job.queue_index].slug;
    }
    return job.record.title[0] != 0 ? job.record.title : job.record.slug;
}

bool install_have(const install_t *entry)
{
    const install_t *known;

    if (!var_exists(entry->var_name, entry->var_type))
    {
        return false;
    }

    known = store_find(entry->slug);

    if (known == NULL)
    {
        return false;
    }
    if (entry->digest[0] != 0 && known->digest[0] != 0)
    {
        return strcmp(entry->digest, known->digest) == 0;
    }
    return strcmp(entry->version, known->version) == 0;
}

static void enqueue(const install_t *entry)
{
    if (job.queue_count >= REQUIRE_MAX || entry->slug[0] == 0 || entry->var_name[0] == 0)
    {
        return;
    }
    if (install_have(entry))
    {
        return;
    }

    job.queue[job.queue_count] = *entry;
    job.queue_count++;
}

static void record_installed(const install_t *entry, uint32_t written)
{
    program_t written_record;

    memset(&written_record, 0, sizeof written_record);

    strncpy(written_record.slug, entry->slug, SLUG_MAX - 1);
    strncpy(written_record.title, entry->title, TITLE_MAX - 1);
    strncpy(written_record.var_name, job.var_name, VARNAME_MAX - 1);
    strncpy(written_record.version, entry->version, VERSION_TEXT - 1);
    strncpy(written_record.digest, entry->digest, DIGEST_TEXT - 1);

    written_record.size = written;
    written_record.var_type = job.var_type;
    written_record.archived = job.archived;

    store_record(&written_record);
}

static void on_head(const char *name, const char *value)
{
    if (strcasecmp(name, "X-CEagle-Var-Name") == 0 && value[0] != 0)
    {
        strncpy(job.var_name, value, VARNAME_MAX - 1);
        job.var_name[VARNAME_MAX - 1] = 0;
        return;
    }

    if (strcasecmp(name, "X-CEagle-Var-Type") == 0)
    {
        if (strcmp(value, "appvar") == 0)
        {
            job.var_type = VAR_APPVAR;
        }
        else if (strcmp(value, "protected") == 0)
        {
            job.var_type = VAR_PROTECTED;
        }
        else
        {
            job.var_type = VAR_PROGRAM;
        }
        return;
    }

    if (strcasecmp(name, "X-CEagle-Var-Archived") == 0)
    {
        job.archived = strcmp(value, "true") == 0;
        return;
    }

    if (strcasecmp(name, "X-CEagle-Var-Size") == 0)
    {
        uint32_t size = 0;
        uint8_t index;

        for (index = 0; value[index] >= '0' && value[index] <= '9'; index++)
        {
            size = size * 10u + (uint32_t)(value[index] - '0');
        }

        job.expected = size;
    }
}

static bool on_chunk(const void *data, size_t length)
{
    if (job.state == JOB_FAILED)
    {
        return false;
    }

    if (net_status() != 200)
    {
        fail("the server refused the download");
        return false;
    }

    if (!job.opened)
    {
        if (job.var_name[0] == 0)
        {
            fail("the server sent no variable name");
            return false;
        }

        var_delete(job.var_name, job.var_type);

        job.handle = ti_OpenVar(job.var_name, "w", os_type(job.var_type));

        if (job.handle == 0)
        {
            fail("could not create the variable");
            return false;
        }

        job.opened = true;
        job.state = JOB_WRITING;

        if (job.expected > 0)
        {
            if (ti_Resize(job.expected, job.handle) <= 0)
            {
                fail("not enough memory on the calculator");
                return false;
            }

            ti_Rewind(job.handle);
        }
    }

    if (ti_Write(data, 1, length, job.handle) != length)
    {
        fail("out of memory on the calculator");
        return false;
    }

    job.written += (uint32_t)length;

    return true;
}

static void on_done(uint16_t status, const char *body, size_t length)
{
    (void)body;
    (void)length;

    if (job.state == JOB_FAILED)
    {
        return;
    }

    if (status == 0)
    {
        fail(net_error()[0] != 0 ? net_error() : "the server could not be reached");
        return;
    }

    if (status == 404)
    {
        fail(job.dependency ? "a library it needs is no longer published"
                            : "that program is no longer published");
        return;
    }

    if (status != 200)
    {
        fail("the server refused the download");
        return;
    }

    if (!job.opened)
    {
        fail("the server sent an empty program");
        return;
    }

    if (job.expected != 0 && job.written != job.expected)
    {
        fail("the download was cut short");
        return;
    }

    if (job.archived)
    {
        ti_SetArchiveStatus(true, job.handle);
    }

    ti_Close(job.handle);
    job.opened = false;

    if (job.dependency)
    {
        record_installed(&job.queue[job.queue_index], job.written);

        job.queue_index++;
        job.want = WANT_ITEM;
        job.state = JOB_RUNNING;
        message("sent, next one...");

        programs_mark_installed();
        return;
    }

    job.state = JOB_DONE;
    message(job.queue_count > 0 ? "sent with everything it needs"
                                : "sent to the calculator");

    if (job.record.slug[0] != 0)
    {
        strncpy(job.record.var_name, job.var_name, VARNAME_MAX - 1);
        job.record.var_name[VARNAME_MAX - 1] = 0;
        job.record.var_type = job.var_type;
        job.record.archived = job.archived;
        job.record.size = job.written;

        store_record(&job.record);
    }

    programs_mark_installed();

    if (detail_ready)
    {
        detail.installed = var_exists(detail.var_name, detail.var_type);
    }
}

static void on_requires(uint16_t status, const char *body, size_t length)
{
    json_t root;
    json_t list;
    uint16_t count;
    uint16_t index;

    if (status == 0)
    {
        fail(net_error()[0] != 0 ? net_error() : "the server could not be reached");
        return;
    }

    /*
     * A server that does not answer this route is one that publishes nothing
     * with dependencies, so the program is sent on its own rather than refused.
     */
    if (status == 200 && json_value(body, body + length, &root) && root.kind == JSON_OBJECT &&
        json_member(&root, "programs", &list) && list.kind == JSON_ARRAY)
    {
        count = json_count(&list);

        if (count > REQUIRE_MAX)
        {
            job.overflow = true;
        }

        for (index = 0; index < count && job.queue_count < REQUIRE_MAX; index++)
        {
            install_t entry;
            json_t item;

            if (!json_at(&list, index, &item) || item.kind != JSON_OBJECT)
            {
                continue;
            }

            memset(&entry, 0, sizeof entry);

            json_text_member(&item, "slug", entry.slug, SLUG_MAX);
            json_text_member(&item, "title", entry.title, TITLE_MAX);
            json_text_member(&item, "var_name", entry.var_name, VARNAME_MAX);
            json_text_member(&item, "version", entry.version, VERSION_TEXT);
            json_text_member(&item, "checksum", entry.digest, DIGEST_TEXT);

            entry.size = (uint32_t)json_number_member(&item, "size", 0);
            entry.var_type = var_type_from(&item);
            entry.archived = json_flag(&item, "archived");

            enqueue(&entry);
        }
    }

    job.queue_index = 0;

    if (job.overflow)
    {
        fail("it needs more libraries than this app can send");
        return;
    }

    job.want = WANT_ITEM;
    job.state = JOB_RUNNING;
}

static void start_requires(void)
{
    char path[SLUG_MAX + 32];

    message("checking what it needs...");

    strcpy(path, "/api/v1/programs/");
    strncat(path, job.record.slug, SLUG_MAX - 1);
    strcat(path, "/requires");

    if (!net_get(net_url(server_url(), path), on_requires))
    {
        fail(net_error()[0] != 0 ? net_error() : "could not ask the server");
    }
}

static void start_item(void)
{
    char path[SLUG_MAX + 24];
    const install_t *entry = NULL;
    char text[24];

    job.dependency = job.queue_index < job.queue_count;

    if (job.dependency)
    {
        entry = &job.queue[job.queue_index];

        strncpy(job.slug, entry->slug, SLUG_MAX - 1);
        job.slug[SLUG_MAX - 1] = 0;
        strncpy(job.var_name, entry->var_name, VARNAME_MAX - 1);
        job.var_name[VARNAME_MAX - 1] = 0;

        job.var_type = entry->var_type;
        job.archived = entry->archived;
        job.expected = entry->size;
    }
    else
    {
        strncpy(job.slug, job.record.slug, SLUG_MAX - 1);
        job.slug[SLUG_MAX - 1] = 0;
        strncpy(job.var_name, job.record.var_name, VARNAME_MAX - 1);
        job.var_name[VARNAME_MAX - 1] = 0;

        job.var_type = job.record.var_type;
        job.archived = job.record.archived;
        job.expected = job.record.size;
    }

    job.written = 0;
    job.opened = false;
    job.state = JOB_RUNNING;

    if (job.queue_count > 0)
    {
        strcpy(text, "sending ");
        ui_number(text + 8, install_step());
        strcat(text, " of ");
        ui_number(text + strlen(text), install_steps());
        message(text);
    }
    else
    {
        message("asking the server...");
    }

    strcpy(path, "/d/");
    strncat(path, job.slug, SLUG_MAX - 1);
    strcat(path, "/raw");

    if (!net_stream(net_url(server_url(), path), on_chunk, on_head, on_done))
    {
        fail(net_error()[0] != 0 ? net_error() : "could not start the download");
    }
}

/*
 * Requests are started from the main loop rather than from a completion
 * callback, because the client is still winding the finished transfer down
 * while the callback runs and would refuse a second one.
 */
void install_pump(void)
{
    uint8_t wanted = job.want;

    if (wanted == WANT_NOTHING || net_busy())
    {
        return;
    }

    job.want = WANT_NOTHING;

    if (wanted == WANT_REQUIRES)
    {
        start_requires();
        return;
    }

    start_item();
}

void install_begin(const program_t *program)
{
    uint8_t index;

    if (program == NULL || program->slug[0] == 0 || net_busy())
    {
        return;
    }

    install_reset();

    job.record = *program;
    job.state = JOB_RUNNING;

    /*
     * The detail view has already been told what this program needs, so it is
     * reused rather than asked for a second time.
     */
    if (detail_ready && strcmp(detail.slug, program->slug) == 0)
    {
        for (index = 0; index < require_count; index++)
        {
            enqueue(&requirements[index]);
        }

        job.queue_index = 0;
        job.overflow = require_overflow;

        if (job.overflow)
        {
            fail("it needs more libraries than this app can send");
            return;
        }

        job.want = WANT_ITEM;
        message("asking the server...");
        return;
    }

    job.want = WANT_REQUIRES;
    message("checking what it needs...");
}
