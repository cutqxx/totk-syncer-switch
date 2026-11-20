#include "save_write.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs_compat.h"
#include "save_rules.h"

#define CHUNK_BYTES (256 * 1024)
#define METADATA_COST_BYTES (4 * 1024) /* journal use counted for one remove or mkdir */
#define FULL_PATH_SIZE (2 * TS_PATH_SIZE + 8)
#define CHILD_NAME_SIZE 264 /* any d_name fits */

typedef struct {
    const file_ops_t *ops;
    const console_port_t *console;
    const char *root;
    int64_t budget;
    int64_t pending;
    bool is_commit_after_each_file;
    save_write_result_t *out;
} writer_t;

static bool fail(writer_t *w, const char *format, ...) __attribute__((format(printf, 2, 3)));

static bool fail(writer_t *w, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(w->out->reason, sizeof w->out->reason, format, args);
    va_end(args);
    return false;
}

static void full_path(const writer_t *w, const char *rel, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s", w->root, rel);
}

int64_t save_write_budget(int64_t journal_bytes)
{
    return journal_bytes > 0 ? journal_bytes / 4 * 3 : SAVE_WRITE_FALLBACK_BUDGET;
}

static bool commit(writer_t *w)
{
    uint32_t error_code = 0;
    if (!w->console->commit_save(w->console->ctx, &error_code)) {
        return fail(w, "commit failed (error 0x%x)", (unsigned)error_code);
    }
    w->out->commits++;
    w->out->is_changed = true;
    w->pending = 0;
    return true;
}

static bool account_metadata(writer_t *w)
{
    w->out->is_changed = true;
    w->pending += METADATA_COST_BYTES;
    return w->pending < w->budget || commit(w);
}

/* --- removing what is there ------------------------------------------------------------------------------------- */

typedef struct {
    char (*names)[CHILD_NAME_SIZE];
    int count;
    int capacity;
} name_list_t;

static bool list_children(const char *dir_path, name_list_t *list)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return false;
    }
    bool is_ok = true;
    for (struct dirent *item = readdir(dir); item != NULL && is_ok; item = readdir(dir)) {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
            continue;
        }
        if (list->count == list->capacity) {
            int capacity = list->capacity == 0 ? 32 : list->capacity * 2;
            void *grown = realloc(list->names, (size_t)capacity * sizeof list->names[0]);
            is_ok = grown != NULL;
            if (is_ok) {
                list->names = grown;
                list->capacity = capacity;
            }
        }
        if (is_ok) {
            snprintf(list->names[list->count++], CHILD_NAME_SIZE, "%s", item->d_name);
        }
    }
    closedir(dir);
    return is_ok;
}

static bool remove_tree(writer_t *w, const char *rel_dir);

static bool remove_child(writer_t *w, const char *rel_dir, const char *name)
{
    char rel[TS_PATH_SIZE + CHILD_NAME_SIZE];
    char path[FULL_PATH_SIZE + CHILD_NAME_SIZE];
    struct stat info;
    snprintf(rel, sizeof rel, "%s%s%s", rel_dir, rel_dir[0] != '\0' ? "/" : "", name);
    snprintf(path, sizeof path, "%s/%s", w->root, rel);
    if (stat(path, &info) != 0) {
        return fail(w, "cannot read %s: %s", rel, strerror(errno));
    }
    if (S_ISDIR(info.st_mode)) {
        if (!remove_tree(w, rel)) {
            return false;
        }
        if (!w->ops->remove_dir(w->ops->ctx, path)) {
            return fail(w, "cannot remove %s: %s", rel, strerror(errno));
        }
    } else if (!w->ops->remove_file(w->ops->ctx, path)) {
        return fail(w, "cannot remove %s: %s", rel, strerror(errno));
    }
    return account_metadata(w);
}

static bool remove_tree(writer_t *w, const char *rel_dir)
{
    char path[FULL_PATH_SIZE];
    name_list_t children = { NULL, 0, 0 };
    full_path(w, rel_dir, path, sizeof path);
    if (!list_children(path, &children)) {
        free(children.names);
        return fail(w, "cannot list %s: %s", rel_dir[0] != '\0' ? rel_dir : "the save", strerror(errno));
    }
    bool is_ok = true;
    for (int i = 0; i < children.count && is_ok; i++) {
        is_ok = remove_child(w, rel_dir, children.names[i]);
    }
    free(children.names);
    return is_ok;
}

/* --- writing the ZIP -------------------------------------------------------------------------------------------- */

static bool make_dir_with_parents(writer_t *w, const char *rel)
{
    char partial[TS_PATH_SIZE];
    size_t length = strlen(rel);
    for (size_t i = 0; i <= length; i++) {
        if (rel[i] != '/' && rel[i] != '\0') {
            continue;
        }
        char path[FULL_PATH_SIZE];
        memcpy(partial, rel, i);
        partial[i] = '\0';
        full_path(w, partial, path, sizeof path);
        struct stat info;
        if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
            continue;
        }
        if (!w->ops->make_dir(w->ops->ctx, path)) {
            return fail(w, "cannot create %s: %s", partial, strerror(errno));
        }
        if (!account_metadata(w)) {
            return false;
        }
    }
    return true;
}

static bool make_parent_dirs(writer_t *w, const char *rel_file)
{
    const char *slash = strrchr(rel_file, '/');
    if (slash == NULL) {
        return true;
    }
    char parent[TS_PATH_SIZE];
    snprintf(parent, sizeof parent, "%.*s", (int)(slash - rel_file), rel_file);
    return make_dir_with_parents(w, parent);
}

static bool close_file(writer_t *w, void *file, const char *rel)
{
    if (!w->ops->close(w->ops->ctx, file)) {
        return fail(w, "cannot write %s: %s", rel, strerror(errno));
    }
    return true;
}

/* Closes, commits and reopens for appending, so the journal never holds more than the budget. */
static bool make_room(writer_t *w, void **file, const char *path, const char *rel)
{
    if (!close_file(w, *file, rel)) {
        *file = NULL;
        return false;
    }
    *file = NULL;
    if (!commit(w)) {
        return false;
    }
    *file = w->ops->open_write(w->ops->ctx, path, true);
    return *file != NULL ? true : fail(w, "cannot write %s: %s", rel, strerror(errno));
}

static bool write_content(writer_t *w, void **file, const char *path, const char *rel, const unsigned char *data,
                          size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        if (w->pending >= w->budget && !make_room(w, file, path, rel)) {
            return false;
        }
        int64_t room = w->budget - w->pending;
        size_t chunk = size - offset;
        chunk = chunk > CHUNK_BYTES ? CHUNK_BYTES : chunk;
        chunk = (int64_t)chunk > room ? (size_t)room : chunk;
        if (!w->ops->write(w->ops->ctx, *file, data + offset, chunk)) {
            return fail(w, "cannot write %s: %s", rel, strerror(errno));
        }
        w->pending += (int64_t)chunk;
        offset += chunk;
    }
    return true;
}

static bool write_file(writer_t *w, zip_reader_t *zip, int index, const char *rel)
{
    char path[FULL_PATH_SIZE];
    unsigned char *data = NULL;
    size_t size = 0;
    if (!zip_reader_extract(zip, index, &data, &size)) {
        return fail(w, "cannot unpack %s (out of memory?)", rel);
    }
    full_path(w, rel, path, sizeof path);
    bool is_ok = make_parent_dirs(w, rel);
    void *file = is_ok ? w->ops->open_write(w->ops->ctx, path, false) : NULL;
    if (is_ok && file == NULL) {
        is_ok = fail(w, "cannot write %s: %s", rel, strerror(errno));
    }
    if (is_ok) {
        w->out->is_changed = true;
        is_ok = write_content(w, &file, path, rel, data, size);
    }
    if (file != NULL) {
        bool is_closed = close_file(w, file, rel);
        is_ok = is_ok && is_closed;
    }
    free(data);
    return is_ok && (!w->is_commit_after_each_file || commit(w));
}

static bool is_written_entry(const zip_entry_t *entry)
{
    return !save_rules_is_junk(entry->path);
}

static int64_t total_file_bytes(zip_reader_t *zip)
{
    int64_t total = 0;
    for (int i = 0; i < zip_reader_count(zip); i++) {
        zip_entry_t entry;
        zip_reader_entry(zip, i, &entry);
        total += !entry.is_directory && is_written_entry(&entry) ? entry.size : 0;
    }
    return total;
}

static bool write_entries(writer_t *w, zip_reader_t *zip)
{
    for (int i = 0; i < zip_reader_count(zip); i++) {
        zip_entry_t entry;
        zip_reader_entry(zip, i, &entry);
        if (!is_written_entry(&entry)) {
            continue;
        }
        bool is_ok = entry.is_directory ? make_dir_with_parents(w, entry.path) : write_file(w, zip, i, entry.path);
        if (!is_ok) {
            return false;
        }
    }
    return true;
}

void save_write_zip(const file_ops_t *ops, const console_port_t *console, const char *root, zip_reader_t *zip,
                    int64_t journal_bytes, save_write_result_t *out)
{
    memset(out, 0, sizeof *out);
    writer_t w = { ops, console, root, save_write_budget(journal_bytes), 0, false, out };
    w.is_commit_after_each_file = total_file_bytes(zip) > w.budget;
    out->is_ok = remove_tree(&w, "") && write_entries(&w, zip) && commit(&w);
}
