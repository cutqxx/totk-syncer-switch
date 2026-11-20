#ifndef TS_SAVE_TREE_H
#define TS_SAVE_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ts_limits.h"

typedef enum {
    TREE_OK,
    TREE_ERR_TOO_MANY_ENTRIES, /* more than TS_MAX_SAVE_ENTRIES files and folders */
    TREE_ERR_BAD_NAME,         /* problem_path names the entry the PC launcher would reject */
    TREE_ERR_READ,             /* problem_path names the entry that could not be listed */
    TREE_ERR_MEMORY
} tree_status_t;

typedef struct {
    char path[TS_PATH_SIZE]; /* relative, '/' separated */
    bool is_directory;
    int64_t size;
} save_tree_entry_t;

/* The save as the PC sees it: every folder and file except junk and .nx_save_meta.bin (save_rules), directories
   first, then files, each group sorted by path bytes (the fingerprint's order for files). */
typedef struct {
    save_tree_entry_t *items;
    int count;
    int files;
    int directories;
    int64_t total_bytes;        /* summed file sizes */
    int64_t largest_file_bytes;
    char problem_path[TS_PATH_SIZE];
} save_tree_t;

/* Lists the tree under root without reading any file. Always call save_tree_free afterwards. */
tree_status_t save_tree_scan(const char *root, save_tree_t *tree);

void save_tree_free(save_tree_t *tree);

/* "<root>/<rel_path>" */
void save_tree_full_path(const char *root, const char *rel_path, char *out, size_t out_size);

#endif
