#include "save_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs_compat.h"
#include "save_rules.h"
#include "zip_writer.h"

typedef struct {
    char path[TS_PATH_SIZE];
    bool is_directory;
    int64_t size;
} pack_entry_t;

typedef struct {
    pack_entry_t *items;
    int count;
    int64_t largest_file_bytes;
} entry_list_t;

typedef struct {
    const char *root;
    entry_list_t *list;
    pack_stats_t *stats;
} scan_t;

static void join_path(char *out, size_t out_size, const char *left, const char *right)
{
    if (left[0] == '\0') {
        snprintf(out, out_size, "%s", right);
    } else {
        snprintf(out, out_size, "%s/%s", left, right);
    }
}

static pack_status_t fail_at(scan_t *scan, pack_status_t status, const char *rel_path)
{
    snprintf(scan->stats->problem_path, sizeof scan->stats->problem_path, "%s", rel_path);
    return status;
}

static pack_status_t add_entry(scan_t *scan, const char *rel_path, bool is_directory, int64_t size)
{
    if (scan->list->count >= TS_MAX_SAVE_ENTRIES) {
        return fail_at(scan, PACK_ERR_TOO_MANY_ENTRIES, rel_path);
    }
    pack_entry_t *entry = &scan->list->items[scan->list->count++];
    snprintf(entry->path, sizeof entry->path, "%s", rel_path);
    entry->is_directory = is_directory;
    entry->size = size;
    if (is_directory) {
        scan->stats->directories++;
        return PACK_OK;
    }
    scan->stats->files++;
    scan->stats->total_bytes += size;
    if (size > scan->list->largest_file_bytes) {
        scan->list->largest_file_bytes = size;
    }
    return PACK_OK;
}

static pack_status_t scan_directory(scan_t *scan, const char *rel_dir);

static pack_status_t scan_child(scan_t *scan, const char *rel_path, const struct stat *info)
{
    bool is_directory = S_ISDIR(info->st_mode);
    if (!save_rules_is_junk(rel_path)) {
        if (!save_rules_is_safe_path(rel_path) || strlen(rel_path) >= TS_PATH_SIZE - 1) {
            return fail_at(scan, PACK_ERR_BAD_NAME, rel_path);
        }
        pack_status_t added = add_entry(scan, rel_path, is_directory, (int64_t)info->st_size);
        if (added != PACK_OK) {
            return added;
        }
    }
    return is_directory ? scan_directory(scan, rel_path) : PACK_OK;
}

static pack_status_t scan_named_child(scan_t *scan, const char *rel_dir, const char *name)
{
    char rel_path[TS_PATH_SIZE + 64];
    char full_path[2 * TS_PATH_SIZE + 64];
    struct stat info;
    join_path(rel_path, sizeof rel_path, rel_dir, name);
    join_path(full_path, sizeof full_path, scan->root, rel_path);
    if (stat(full_path, &info) != 0) {
        return fail_at(scan, PACK_ERR_READ, rel_path);
    }
    if (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)) {
        return PACK_OK;
    }
    return scan_child(scan, rel_path, &info);
}

static pack_status_t scan_directory(scan_t *scan, const char *rel_dir)
{
    char full_path[2 * TS_PATH_SIZE + 64];
    join_path(full_path, sizeof full_path, scan->root, rel_dir);
    DIR *directory = opendir(full_path);
    if (directory == NULL) {
        return fail_at(scan, PACK_ERR_READ, rel_dir);
    }
    pack_status_t status = PACK_OK;
    for (struct dirent *item = readdir(directory); item != NULL && status == PACK_OK; item = readdir(directory)) {
        if (strcmp(item->d_name, ".") != 0 && strcmp(item->d_name, "..") != 0) {
            status = scan_named_child(scan, rel_dir, item->d_name);
        }
    }
    closedir(directory);
    return status;
}

static int compare_entries(const void *left, const void *right)
{
    const pack_entry_t *a = left;
    const pack_entry_t *b = right;
    if (a->is_directory != b->is_directory) {
        return a->is_directory ? -1 : 1;
    }
    return strcmp(a->path, b->path);
}

static pack_status_t read_whole_file(const char *root, const pack_entry_t *entry, unsigned char *buffer)
{
    char full_path[2 * TS_PATH_SIZE + 64];
    join_path(full_path, sizeof full_path, root, entry->path);
    FILE *file = fopen(full_path, "rb");
    if (file == NULL) {
        return PACK_ERR_READ;
    }
    size_t wanted = (size_t)entry->size;
    bool is_complete = fread(buffer, 1, wanted, file) == wanted;
    fclose(file);
    return is_complete ? PACK_OK : PACK_ERR_READ;
}

static pack_status_t write_entry(zip_writer_t *writer, const char *root, const pack_entry_t *entry, unsigned char *buffer)
{
    if (entry->is_directory) {
        return zip_writer_add_directory(writer, entry->path) ? PACK_OK : PACK_ERR_ZIP;
    }
    if (read_whole_file(root, entry, buffer) != PACK_OK) {
        return PACK_ERR_READ;
    }
    return zip_writer_add_file(writer, entry->path, buffer, (size_t)entry->size) ? PACK_OK : PACK_ERR_ZIP;
}

static pack_status_t write_archive(const char *root, const entry_list_t *list, zip_writer_t *writer, pack_stats_t *stats)
{
    unsigned char *buffer = malloc((size_t)list->largest_file_bytes + 1);
    if (buffer == NULL) {
        return PACK_ERR_ZIP;
    }
    pack_status_t status = PACK_OK;
    for (int i = 0; i < list->count && status == PACK_OK; i++) {
        status = write_entry(writer, root, &list->items[i], buffer);
        if (status == PACK_ERR_READ) {
            snprintf(stats->problem_path, sizeof stats->problem_path, "%s", list->items[i].path);
        }
    }
    free(buffer);
    return status;
}

static pack_status_t pack_entries(const char *root, const struct tm *entry_time, entry_list_t *list, body_t *zip,
                                  pack_stats_t *stats)
{
    qsort(list->items, (size_t)list->count, sizeof list->items[0], compare_entries);
    zip_writer_t *writer = zip_writer_open(entry_time, list->count, (size_t)stats->total_bytes);
    if (writer == NULL) {
        return PACK_ERR_ZIP;
    }
    pack_status_t status = write_archive(root, list, writer, stats);
    if (status != PACK_OK) {
        zip_writer_abort(writer);
        return status;
    }
    if (!zip_writer_finish(writer, zip)) {
        return PACK_ERR_ZIP;
    }
    stats->zip_bytes = zip->size;
    return PACK_OK;
}

pack_status_t save_pack_dir(const char *root, const struct tm *entry_time, int64_t max_total_bytes, body_t *zip,
                            pack_stats_t *stats)
{
    memset(stats, 0, sizeof *stats);
    entry_list_t list = { calloc(TS_MAX_SAVE_ENTRIES, sizeof(pack_entry_t)), 0, 0 };
    if (list.items == NULL) {
        return PACK_ERR_ZIP;
    }
    scan_t scan = { root, &list, stats };
    pack_status_t status = scan_directory(&scan, "");
    if (status == PACK_OK && stats->total_bytes > max_total_bytes) {
        status = PACK_ERR_TOO_LARGE;
    }
    if (status == PACK_OK) {
        status = pack_entries(root, entry_time, &list, zip, stats);
    }
    free(list.items);
    return status;
}
