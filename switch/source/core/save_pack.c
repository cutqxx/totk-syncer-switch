#include "save_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "save_tree.h"
#include "zip_writer.h"

static pack_status_t pack_status_of(tree_status_t status)
{
    switch (status) {
    case TREE_OK: return PACK_OK;
    case TREE_ERR_TOO_MANY_ENTRIES: return PACK_ERR_TOO_MANY_ENTRIES;
    case TREE_ERR_BAD_NAME: return PACK_ERR_BAD_NAME;
    case TREE_ERR_READ: return PACK_ERR_READ;
    default: return PACK_ERR_ZIP;
    }
}

static pack_status_t read_whole_file(const char *root, const save_tree_entry_t *entry, unsigned char *buffer)
{
    char full_path[2 * TS_PATH_SIZE + 64];
    save_tree_full_path(root, entry->path, full_path, sizeof full_path);
    FILE *file = fopen(full_path, "rb");
    if (file == NULL) {
        return PACK_ERR_READ;
    }
    size_t wanted = (size_t)entry->size;
    bool is_complete = fread(buffer, 1, wanted, file) == wanted;
    fclose(file);
    return is_complete ? PACK_OK : PACK_ERR_READ;
}

static pack_status_t write_entry(zip_writer_t *writer, const char *root, const save_tree_entry_t *entry,
                                 unsigned char *buffer)
{
    if (entry->is_directory) {
        return zip_writer_add_directory(writer, entry->path) ? PACK_OK : PACK_ERR_ZIP;
    }
    if (read_whole_file(root, entry, buffer) != PACK_OK) {
        return PACK_ERR_READ;
    }
    return zip_writer_add_file(writer, entry->path, buffer, (size_t)entry->size) ? PACK_OK : PACK_ERR_ZIP;
}

static pack_status_t write_archive(const char *root, const save_tree_t *tree, zip_writer_t *writer, pack_stats_t *stats)
{
    unsigned char *buffer = malloc((size_t)tree->largest_file_bytes + 1);
    if (buffer == NULL) {
        return PACK_ERR_ZIP;
    }
    pack_status_t status = PACK_OK;
    for (int i = 0; i < tree->count && status == PACK_OK; i++) {
        status = write_entry(writer, root, &tree->items[i], buffer);
        if (status == PACK_ERR_READ) {
            snprintf(stats->problem_path, sizeof stats->problem_path, "%s", tree->items[i].path);
        }
    }
    free(buffer);
    return status;
}

static pack_status_t pack_entries(const char *root, const struct tm *entry_time, const save_tree_t *tree, body_t *zip,
                                  pack_stats_t *stats)
{
    zip_writer_t *writer = zip_writer_open(entry_time, tree->count, (size_t)stats->total_bytes);
    if (writer == NULL) {
        return PACK_ERR_ZIP;
    }
    pack_status_t status = write_archive(root, tree, writer, stats);
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
    save_tree_t tree;
    memset(stats, 0, sizeof *stats);
    pack_status_t status = pack_status_of(save_tree_scan(root, &tree));
    stats->files = tree.files;
    stats->directories = tree.directories;
    stats->total_bytes = tree.total_bytes;
    snprintf(stats->problem_path, sizeof stats->problem_path, "%s", tree.problem_path);
    if (status == PACK_OK && stats->total_bytes > max_total_bytes) {
        status = PACK_ERR_TOO_LARGE;
    }
    if (status == PACK_OK) {
        status = pack_entries(root, entry_time, &tree, zip, stats);
    }
    save_tree_free(&tree);
    return status;
}
