#include "file_ops.h"

#include <errno.h>
#include <stdio.h>

#include "fs_compat.h"

static void *stdio_open_write(void *ctx, const char *path, bool is_append)
{
    (void)ctx;
    return fopen(path, is_append ? "ab" : "wb");
}

static bool stdio_write(void *ctx, void *file, const void *data, size_t size)
{
    (void)ctx;
    return size == 0 || fwrite(data, 1, size, file) == size;
}

static bool stdio_close(void *ctx, void *file)
{
    (void)ctx;
    bool is_flushed = fflush(file) == 0;
    int flush_errno = errno;
    bool is_closed = fclose(file) == 0;
    if (!is_flushed) {
        errno = flush_errno;
    }
    return is_flushed && is_closed;
}

static bool is_directory(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool stdio_make_dir(void *ctx, const char *path)
{
    (void)ctx;
    if (ts_mkdir(path) == 0) {
        return true;
    }
    int mkdir_errno = errno;
    if (is_directory(path)) {
        return true;
    }
    errno = mkdir_errno;
    return false;
}

static bool stdio_remove_file(void *ctx, const char *path)
{
    (void)ctx;
    return remove(path) == 0;
}

static bool stdio_remove_dir(void *ctx, const char *path)
{
    (void)ctx;
    return ts_rmdir(path) == 0;
}

static bool stdio_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    return rename(from, to) == 0;
}

const file_ops_t *file_ops_stdio(void)
{
    static const file_ops_t ops = {
        .ctx = NULL,
        .open_write = stdio_open_write,
        .write = stdio_write,
        .close = stdio_close,
        .make_dir = stdio_make_dir,
        .remove_file = stdio_remove_file,
        .remove_dir = stdio_remove_dir,
        .rename = stdio_rename,
    };
    return &ops;
}
