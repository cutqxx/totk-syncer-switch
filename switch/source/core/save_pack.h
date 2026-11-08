#ifndef TS_SAVE_PACK_H
#define TS_SAVE_PACK_H

#include <stdint.h>
#include <time.h>

#include "body.h"
#include "ts_limits.h"

typedef enum {
    PACK_OK,
    PACK_ERR_TOO_LARGE,        /* summed file sizes above the limit; stats.total_bytes holds the sum */
    PACK_ERR_TOO_MANY_ENTRIES, /* more than TS_MAX_SAVE_ENTRIES files and folders */
    PACK_ERR_BAD_NAME,         /* stats.problem_path names the entry the PC launcher would reject */
    PACK_ERR_READ,             /* stats.problem_path names the entry that could not be listed or read */
    PACK_ERR_ZIP               /* out of memory or miniz failure */
} pack_status_t;

typedef struct {
    int files;
    int directories;
    int64_t total_bytes; /* uncompressed size of the packed files */
    int64_t zip_bytes;   /* valid after PACK_OK */
    char problem_path[TS_PATH_SIZE];
} pack_stats_t;

/* Reads the save directory tree under root (nothing is ever written there) and builds the ZIP the PC accepts
   (contracts/save-zip): '/' separators, every directory including empty ones, files in the root of the archive,
   no junk and no .nx_save_meta.bin. Names, entry count and total size are checked before any file is read.
   On PACK_OK *zip holds the archive (caller frees it); otherwise *zip stays empty. */
pack_status_t save_pack_dir(const char *root, const struct tm *entry_time, int64_t max_total_bytes, body_t *zip,
                            pack_stats_t *stats);

#endif
