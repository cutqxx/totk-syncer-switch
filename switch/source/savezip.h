// Building, validating and extracting JKSV-compatible save ZIPs (miniz).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "savemeta.h"
#include "util.h"

// True for files that never belong in a save (and are skipped both ways):
// .nx_save_meta.bin, *.7z, *.zip, *.rar, desktop.ini, Thumbs.db.
bool savezip_is_junk(const char *name);

// Zips everything under `root_dir` (e.g. "totk:/") into memory.
// If `meta` is non-NULL it is stored first as ".nx_save_meta.bin".
// Directory entries ("album/", "slot_00/") are written like JKSV does.
bool savezip_build(const char *root_dir, const uint8_t *meta, Buffer *out, char *err, size_t err_size);

typedef struct {
    char prefix[128];      // folder inside the ZIP that holds the save ("" for root)
    bool has_meta;
    SaveMeta meta;
    unsigned file_count;   // save files (excluding meta/junk)
    uint64_t total_size;   // uncompressed bytes of those files
} SaveZipInfo;

// Checks that the ZIP is intact (CRC), contains a TotK save layout
// (option.sav + slot_00/progress.sav), has no unsafe paths and, if a meta
// file is present, that it belongs to TotK. Nothing is written anywhere.
bool savezip_validate(const uint8_t *data, size_t size, SaveZipInfo *info, char *err, size_t err_size);

typedef struct {
    // Called to commit the save device. NULL on host / for plain folders.
    bool (*commit)(void *ctx);
    void *ctx;
    // Commit before the journal would overflow (0 = only after each file).
    int64_t journal_size;
} CommitHooks;

// Extracts a validated ZIP (only entries under info->prefix, without meta/junk)
// into `dest_root`, committing per the hooks. Commits once more at the end.
bool savezip_extract(const uint8_t *data, size_t size, const SaveZipInfo *info, const char *dest_root,
                     const CommitHooks *hooks, char *err, size_t err_size);
