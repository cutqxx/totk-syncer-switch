#include "test_fs.h"

#include <stdio.h>
#include <string.h>

#include <stdlib.h>

#include "fs_compat.h"
#include "miniz.h"
#include "test_support.h"

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

bool test_fs_make_dir(const char *root, const char *rel)
{
    char child[512];
    char path[512];
    struct stat info;
    snprintf(child, sizeof child, "%s/_", rel);
    make_parent_directories(root, child);
    snprintf(path, sizeof path, "%s/%s", root, rel);
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool copy_file(const char *from, const char *to)
{
    FILE *source = fopen(from, "rb");
    if (source == NULL) {
        return false;
    }
    fseek(source, 0, SEEK_END);
    long size = ftell(source);
    fseek(source, 0, SEEK_SET);
    unsigned char *content = malloc((size_t)size + 1);
    bool is_read = content != NULL && fread(content, 1, (size_t)size, source) == (size_t)size;
    fclose(source);
    FILE *target = is_read ? fopen(to, "wb") : NULL;
    bool is_copied = target != NULL && fwrite(content, 1, (size_t)size, target) == (size_t)size;
    if (target != NULL) {
        fclose(target);
    }
    free(content);
    return is_copied;
}

bool test_fs_copy_tree(const char *src, const char *dst)
{
    DIR *directory = opendir(src);
    bool is_copied = directory != NULL;
    for (struct dirent *item = is_copied ? readdir(directory) : NULL; item != NULL && is_copied; item = readdir(directory)) {
        char from[512];
        char to[512];
        struct stat info;
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
            continue;
        }
        snprintf(from, sizeof from, "%s/%s", src, item->d_name);
        snprintf(to, sizeof to, "%s/%s", dst, item->d_name);
        is_copied = stat(from, &info) == 0;
        if (is_copied && S_ISDIR(info.st_mode)) {
            ts_mkdir(to);
            is_copied = test_fs_copy_tree(from, to);
        } else if (is_copied) {
            is_copied = copy_file(from, to);
        }
    }
    if (directory != NULL) {
        closedir(directory);
    }
    return is_copied;
}

bool test_fs_make_contract_save(char *root_out, size_t root_size)
{
    return test_fs_make_root(root_out, root_size) && test_fs_copy_tree(CONTRACTS_DIR "/save-tree", root_out)
           && test_fs_make_dir(root_out, "album");
}

typedef struct {
    char **lines;
    int count;
    int capacity;
    bool is_ok;
} snapshot_t;

static void add_snapshot_line(snapshot_t *snapshot, const char *line)
{
    if (snapshot->count == snapshot->capacity) {
        snapshot->capacity = snapshot->capacity == 0 ? 64 : snapshot->capacity * 2;
        snapshot->lines = realloc(snapshot->lines, (size_t)snapshot->capacity * sizeof *snapshot->lines);
    }
    snapshot->lines[snapshot->count] = malloc(strlen(line) + 1);
    strcpy(snapshot->lines[snapshot->count++], line);
}

static void snapshot_directory(snapshot_t *snapshot, const char *root, const char *rel_dir);

static void snapshot_entry(snapshot_t *snapshot, const char *root, const char *rel)
{
    char full[512];
    char line[640];
    struct stat info;
    snprintf(full, sizeof full, "%s/%s", root, rel);
    if (stat(full, &info) != 0) {
        snapshot->is_ok = false;
        return;
    }
    if (S_ISDIR(info.st_mode)) {
        snprintf(line, sizeof line, "%s|dir|%lld", rel, (long long)info.st_mtime);
        add_snapshot_line(snapshot, line);
        snapshot_directory(snapshot, root, rel);
        return;
    }
    char *content = test_read_file(full);
    unsigned long crc = content == NULL ? 0 : mz_crc32(MZ_CRC32_INIT, (const unsigned char *)content, (size_t)info.st_size);
    snprintf(line, sizeof line, "%s|file|%lld|%lld|%lu", rel, (long long)info.st_size, (long long)info.st_mtime, crc);
    add_snapshot_line(snapshot, line);
    free(content);
}

static void snapshot_directory(snapshot_t *snapshot, const char *root, const char *rel_dir)
{
    char full[512];
    if (rel_dir[0] == '\0') {
        snprintf(full, sizeof full, "%s", root);
    } else {
        snprintf(full, sizeof full, "%s/%s", root, rel_dir);
    }
    DIR *directory = opendir(full);
    if (directory == NULL) {
        snapshot->is_ok = false;
        return;
    }
    for (struct dirent *item = readdir(directory); item != NULL; item = readdir(directory)) {
        char rel[512];
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
            continue;
        }
        if (rel_dir[0] == '\0') {
            snprintf(rel, sizeof rel, "%s", item->d_name);
        } else {
            snprintf(rel, sizeof rel, "%s/%s", rel_dir, item->d_name);
        }
        snapshot_entry(snapshot, root, rel);
    }
    closedir(directory);
}

static int compare_lines(const void *left, const void *right)
{
    return strcmp(*(char *const *)left, *(char *const *)right);
}

char *test_fs_snapshot(const char *root)
{
    snapshot_t snapshot = { NULL, 0, 0, true };
    snapshot_directory(&snapshot, root, "");
    qsort(snapshot.lines, (size_t)snapshot.count, sizeof *snapshot.lines, compare_lines);
    size_t total = 1;
    for (int i = 0; i < snapshot.count; i++) {
        total += strlen(snapshot.lines[i]) + 1;
    }
    char *text = calloc(total, 1);
    for (int i = 0; i < snapshot.count; i++) {
        strcat(text, snapshot.lines[i]);
        strcat(text, "\n");
        free(snapshot.lines[i]);
    }
    free(snapshot.lines);
    if (!snapshot.is_ok) {
        free(text);
        return NULL;
    }
    return text;
}
