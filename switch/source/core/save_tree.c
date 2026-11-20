#include "save_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs_compat.h"
#include "save_rules.h"

typedef struct {
    const char *root;
    save_tree_t *tree;
} scan_t;

static void join_path(char *out, size_t out_size, const char *left, const char *right)
{
    if (left[0] == '\0') {
        snprintf(out, out_size, "%s", right);
    } else {
        snprintf(out, out_size, "%s/%s", left, right);
    }
}

void save_tree_full_path(const char *root, const char *rel_path, char *out, size_t out_size)
{
    join_path(out, out_size, root, rel_path);
}

static tree_status_t fail_at(scan_t *scan, tree_status_t status, const char *rel_path)
{
    snprintf(scan->tree->problem_path, sizeof scan->tree->problem_path, "%s", rel_path);
    return status;
}

static tree_status_t add_entry(scan_t *scan, const char *rel_path, bool is_directory, int64_t size)
{
    save_tree_t *tree = scan->tree;
    if (tree->count >= TS_MAX_SAVE_ENTRIES) {
        return fail_at(scan, TREE_ERR_TOO_MANY_ENTRIES, rel_path);
    }
    save_tree_entry_t *entry = &tree->items[tree->count++];
    snprintf(entry->path, sizeof entry->path, "%s", rel_path);
    entry->is_directory = is_directory;
    entry->size = size;
    if (is_directory) {
        tree->directories++;
        return TREE_OK;
    }
    tree->files++;
    tree->total_bytes += size;
    if (size > tree->largest_file_bytes) {
        tree->largest_file_bytes = size;
    }
    return TREE_OK;
}

static tree_status_t scan_directory(scan_t *scan, const char *rel_dir);

static tree_status_t scan_child(scan_t *scan, const char *rel_path, const struct stat *info)
{
    bool is_directory = S_ISDIR(info->st_mode);
    if (!save_rules_is_junk(rel_path)) {
        if (!save_rules_is_safe_path(rel_path) || strlen(rel_path) >= TS_PATH_SIZE - 1) {
            return fail_at(scan, TREE_ERR_BAD_NAME, rel_path);
        }
        tree_status_t added = add_entry(scan, rel_path, is_directory, (int64_t)info->st_size);
        if (added != TREE_OK) {
            return added;
        }
    }
    return is_directory ? scan_directory(scan, rel_path) : TREE_OK;
}

static tree_status_t scan_named_child(scan_t *scan, const char *rel_dir, const char *name)
{
    char rel_path[TS_PATH_SIZE + 64];
    char full_path[2 * TS_PATH_SIZE + 64];
    struct stat info;
    join_path(rel_path, sizeof rel_path, rel_dir, name);
    join_path(full_path, sizeof full_path, scan->root, rel_path);
    if (stat(full_path, &info) != 0) {
        return fail_at(scan, TREE_ERR_READ, rel_path);
    }
    if (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)) {
        return TREE_OK;
    }
    return scan_child(scan, rel_path, &info);
}

static tree_status_t scan_directory(scan_t *scan, const char *rel_dir)
{
    char full_path[2 * TS_PATH_SIZE + 64];
    join_path(full_path, sizeof full_path, scan->root, rel_dir);
    DIR *directory = opendir(full_path);
    if (directory == NULL) {
        return fail_at(scan, TREE_ERR_READ, rel_dir);
    }
    tree_status_t status = TREE_OK;
    for (struct dirent *item = readdir(directory); item != NULL && status == TREE_OK; item = readdir(directory)) {
        if (strcmp(item->d_name, ".") != 0 && strcmp(item->d_name, "..") != 0) {
            status = scan_named_child(scan, rel_dir, item->d_name);
        }
    }
    closedir(directory);
    return status;
}

static int compare_entries(const void *left, const void *right)
{
    const save_tree_entry_t *a = left;
    const save_tree_entry_t *b = right;
    if (a->is_directory != b->is_directory) {
        return a->is_directory ? -1 : 1;
    }
    return strcmp(a->path, b->path); /* compares as unsigned char: UTF-8 byte order */
}

tree_status_t save_tree_scan(const char *root, save_tree_t *tree)
{
    memset(tree, 0, sizeof *tree);
    tree->items = calloc(TS_MAX_SAVE_ENTRIES, sizeof(save_tree_entry_t));
    if (tree->items == NULL) {
        return TREE_ERR_MEMORY;
    }
    scan_t scan = { root, tree };
    tree_status_t status = scan_directory(&scan, "");
    if (status == TREE_OK) {
        qsort(tree->items, (size_t)tree->count, sizeof tree->items[0], compare_entries);
    }
    return status;
}

void save_tree_free(save_tree_t *tree)
{
    free(tree->items);
    tree->items = NULL;
    tree->count = 0;
}
