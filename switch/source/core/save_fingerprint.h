#ifndef TS_SAVE_FINGERPRINT_H
#define TS_SAVE_FINGERPRINT_H

#include <stddef.h>

#include "save_tree.h"

#define SAVE_FINGERPRINT_SIZE 72 /* "sha256:" + 64 hex + NUL */

/* The save fingerprint as the PC launcher computes it (the spec's save fingerprint, contracts/fingerprint.py): SHA-256 over
   "<relative path>\n<hex sha256 of the file>\n" for every save file, sorted by path bytes; junk, .nx_save_meta.bin and
   folders do not count. Files are read in chunks, never written. On failure problem_path names the entry
   (TREE_ERR_READ also covers a file that could not be read). */
tree_status_t save_fingerprint_dir(const char *root, char out[SAVE_FINGERPRINT_SIZE], char *problem_path,
                                   size_t problem_size);

#endif
