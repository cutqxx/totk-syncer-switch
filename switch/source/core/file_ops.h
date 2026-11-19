#ifndef TS_FILE_OPS_H
#define TS_FILE_OPS_H

#include <stdbool.h>
#include <stddef.h>

/* Every write the app makes to the SD backups folder or to the mounted TotK save goes through these calls, so tests can
   fail any single step. On failure errno says why. The stdio version works on the host and on the console (fsdev). */
typedef struct {
    void *ctx;
    /* NULL on failure. is_append keeps the content and writes at the end. */
    void *(*open_write)(void *ctx, const char *path, bool is_append);
    bool (*write)(void *ctx, void *file, const void *data, size_t size);
    /* Flushes and closes; false when the data may not have reached the storage. The handle is gone either way. */
    bool (*close)(void *ctx, void *file);
    /* An already existing directory counts as success. */
    bool (*make_dir)(void *ctx, const char *path);
    bool (*remove_file)(void *ctx, const char *path);
    bool (*remove_dir)(void *ctx, const char *path);
    bool (*rename)(void *ctx, const char *from, const char *to);
} file_ops_t;

const file_ops_t *file_ops_stdio(void);

#endif
