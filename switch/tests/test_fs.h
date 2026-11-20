#ifndef TS_TEST_FS_H
#define TS_TEST_FS_H

#include <stdbool.h>
#include <stddef.h>

/* Creates a fresh fake SD root under build/tmp/<n>; returns false on failure. */
bool test_fs_make_root(char *out, size_t out_size);

/* Writes content to <root>/<rel>, creating parent directories. */
bool test_fs_write_file(const char *root, const char *rel, const void *content, size_t size);

/* Creates the directory <root>/<rel> (and its parents); an existing one is fine. */
bool test_fs_make_dir(const char *root, const char *rel);

/* Copies the whole tree under src (hidden and junk files included) into the existing directory dst. */
bool test_fs_copy_tree(const char *src, const char *dst);

/* A fresh root holding a copy of contracts/save-tree plus the empty album/ directory a real save has. */
bool test_fs_make_contract_save(char *root_out, size_t root_size);

/* One line per file and folder: path, kind, size, modification time and CRC of the content. Equal text means
   an untouched tree. Malloc'd; NULL when the tree cannot be read. */
char *test_fs_snapshot(const char *root);

#endif
