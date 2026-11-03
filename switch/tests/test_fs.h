#ifndef TS_TEST_FS_H
#define TS_TEST_FS_H

#include <stdbool.h>
#include <stddef.h>

/* Creates a fresh fake SD root under build/tmp/<n>; returns false on failure. */
bool test_fs_make_root(char *out, size_t out_size);

/* Writes content to <root>/<rel>, creating parent directories. */
bool test_fs_write_file(const char *root, const char *rel, const void *content, size_t size);

#endif
