#include "test_fs.h"

#include <stdio.h>
#include <string.h>

#include "fs_compat.h"

static int next_root_number = 0;

bool test_fs_make_root(char *out, size_t out_size)
{
    ts_mkdir("build");
    ts_mkdir("build/tmp");
    snprintf(out, out_size, "build/tmp/%d", next_root_number++);
    return ts_mkdir(out) == 0;
}

static void make_parent_directories(const char *root, const char *rel)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    for (char *slash = strchr(path + strlen(root) + 1, '/'); slash != NULL; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        ts_mkdir(path);
        *slash = '/';
    }
}

bool test_fs_write_file(const char *root, const char *rel, const void *content, size_t size)
{
    char path[512];
    make_parent_directories(root, rel);
    snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    size_t written = fwrite(content, 1, size, file);
    fclose(file);
    return written == size;
}
