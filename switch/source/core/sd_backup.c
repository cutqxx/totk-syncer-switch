#include "sd_backup.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs_compat.h"
#include "paths.h"
#include "zip_reader.h"

#define PART_SUFFIX ".part"
#define NAME_PREFIX "totk-"
#define MAX_LISTED_BACKUPS 512
#define BACKUP_PATH_SIZE (TS_PATH_SIZE + SD_BACKUP_NAME_SIZE + 8)

static const char *reason_tag(sd_backup_reason_t reason)
{
    return reason == SD_BACKUP_MANUAL ? "manual" : "before-pull";
}

static void backup_dir(const char *sd_root, char *out, size_t out_size)
{
    paths_fs(sd_root, TS_REL_BACKUP_DIR, out, out_size);
}

static void backup_path(const char *sd_root, const char *name, const char *suffix, char *out, size_t out_size)
{
    char dir[TS_PATH_SIZE];
    backup_dir(sd_root, dir, sizeof dir);
    snprintf(out, out_size, "%s/%s%s", dir, name, suffix);
}

static bool fail(sd_backup_result_t *out, const char *step, const char *what)
{
    snprintf(out->error, sizeof out->error, "%s %s: %s", step, what, strerror(errno));
    return false;
}

static bool make_backup_dirs(const file_ops_t *ops, const char *sd_root, sd_backup_result_t *out)
{
    static const char *const levels[] = { "switch", "switch/totk-sync", TS_REL_BACKUP_DIR };
    for (size_t i = 0; i < sizeof levels / sizeof levels[0]; i++) {
        char path[TS_PATH_SIZE];
        paths_fs(sd_root, levels[i], path, sizeof path);
        if (!ops->make_dir(ops->ctx, path)) {
            char shown[TS_PATH_SIZE];
            paths_display(levels[i], shown, sizeof shown);
            return fail(out, "cannot create", shown);
        }
    }
    return true;
}

static bool exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

static bool write_part(const file_ops_t *ops, const char *part_path, const body_t *zip, sd_backup_result_t *out)
{
    void *file = ops->open_write(ops->ctx, part_path, false);
    if (file == NULL) {
        return fail(out, "cannot write", out->display_path);
    }
    bool is_written = ops->write(ops->ctx, file, zip->data, (size_t)zip->size);
    int write_errno = errno;
    bool is_closed = ops->close(ops->ctx, file);
    if (!is_written) {
        errno = write_errno;
    }
    return is_written && is_closed ? true : fail(out, "cannot write", out->display_path);
}

static bool read_whole_file(const char *path, body_t *out)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    bool is_read = fseek(file, 0, SEEK_END) == 0;
    long size = is_read ? ftell(file) : -1;
    is_read = size > 0 && fseek(file, 0, SEEK_SET) == 0;
    unsigned char *data = is_read ? malloc((size_t)size) : NULL;
    is_read = data != NULL && fread(data, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (!is_read) {
        free(data);
        return false;
    }
    return body_adopt(out, data, size);
}

static bool every_entry_reads_back(const body_t *copy, int expected_entries)
{
    zip_reader_t *zip = zip_reader_open(copy->data, (size_t)copy->size);
    bool is_good = zip != NULL && zip_reader_count(zip) == expected_entries;
    for (int i = 0; is_good && i < zip_reader_count(zip); i++) {
        zip_entry_t entry;
        zip_reader_entry(zip, i, &entry);
        if (entry.is_directory) {
            continue;
        }
        unsigned char *data = NULL;
        size_t size = 0;
        is_good = zip_reader_extract(zip, i, &data, &size);
        free(data);
    }
    zip_reader_close(zip);
    return is_good;
}

static int count_entries(const body_t *zip)
{
    zip_reader_t *reader = zip_reader_open(zip->data, (size_t)zip->size);
    int count = reader != NULL ? zip_reader_count(reader) : -1;
    zip_reader_close(reader);
    return count;
}

static bool verify(const char *path, const body_t *zip, sd_backup_result_t *out)
{
    body_t copy = { NULL, 0 };
    bool is_same_size = read_whole_file(path, &copy) && copy.size == zip->size;
    int expected_entries = count_entries(zip);
    bool is_verified = is_same_size && expected_entries >= 0 && every_entry_reads_back(&copy, expected_entries);
    body_free(&copy);
    if (!is_verified) {
        snprintf(out->error, sizeof out->error, "backup %s does not read back correctly", out->display_path);
    }
    return is_verified;
}

static bool is_digits(const char *text, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

bool sd_backup_is_own_name(const char *name)
{
    /* totk-YYYYMMDD-HHMMSS- is 21 characters */
    size_t prefix = strlen(NAME_PREFIX);
    if (strncmp(name, NAME_PREFIX, prefix) != 0 || strlen(name) < prefix + 16) {
        return false;
    }
    const char *at = name + prefix;
    if (!is_digits(at, 8) || at[8] != '-' || !is_digits(at + 9, 6) || at[15] != '-') {
        return false;
    }
    const char *tag = at + 16;
    return strcmp(tag, "manual.zip") == 0 || strcmp(tag, "before-pull.zip") == 0;
}

static int compare_names_newest_first(const void *left, const void *right)
{
    /* The timestamp follows the same prefix, so the name order is the time order. */
    return strcmp(*(char *const *)right, *(char *const *)left);
}

static int prune(const file_ops_t *ops, const char *sd_root, int keep, const char *protected_name)
{
    char dir_path[TS_PATH_SIZE];
    backup_dir(sd_root, dir_path, sizeof dir_path);
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return 0;
    }
    char *names[MAX_LISTED_BACKUPS];
    int count = 0;
    for (struct dirent *item = readdir(dir); item != NULL && count < MAX_LISTED_BACKUPS; item = readdir(dir)) {
        if (sd_backup_is_own_name(item->d_name)) {
            names[count] = malloc(strlen(item->d_name) + 1);
            if (names[count] != NULL) {
                strcpy(names[count++], item->d_name);
            }
        }
    }
    closedir(dir);
    qsort(names, (size_t)count, sizeof names[0], compare_names_newest_first);
    int kept = 0;
    int deleted = 0;
    for (int i = 0; i < count; i++) {
        bool is_protected = strcmp(names[i], protected_name) == 0;
        if (is_protected || kept < keep) {
            kept++;
        } else {
            char path[TS_PATH_SIZE + 260];
            backup_path(sd_root, names[i], "", path, sizeof path);
            deleted += ops->remove_file(ops->ctx, path) ? 1 : 0;
        }
        free(names[i]);
    }
    return deleted;
}

static void name_backup(const struct tm *now, sd_backup_reason_t reason, sd_backup_result_t *out)
{
    snprintf(out->name, sizeof out->name, NAME_PREFIX "%04u%02u%02u-%02u%02u%02u-%s.zip",
             (unsigned)(now->tm_year + 1900) % 10000u, (unsigned)(now->tm_mon + 1) % 100u, (unsigned)now->tm_mday % 100u,
             (unsigned)now->tm_hour % 100u, (unsigned)now->tm_min % 100u, (unsigned)now->tm_sec % 100u, reason_tag(reason));
    char rel[sizeof TS_REL_BACKUP_DIR + SD_BACKUP_NAME_SIZE];
    snprintf(rel, sizeof rel, "%s/%s", TS_REL_BACKUP_DIR, out->name);
    paths_display(rel, out->display_path, sizeof out->display_path);
}

static bool place_and_verify(const file_ops_t *ops, const char *part_path, const char *final_path, const body_t *zip,
                             sd_backup_result_t *out)
{
    if (!write_part(ops, part_path, zip, out)) {
        ops->remove_file(ops->ctx, part_path);
        return false;
    }
    if (!ops->rename(ops->ctx, part_path, final_path)) {
        fail(out, "cannot finish", out->display_path);
        ops->remove_file(ops->ctx, part_path);
        return false;
    }
    if (!verify(final_path, zip, out)) {
        ops->remove_file(ops->ctx, final_path);
        return false;
    }
    return true;
}

bool sd_backup_write(const file_ops_t *ops, const char *sd_root, const struct tm *now, sd_backup_reason_t reason,
                     const body_t *zip, int keep, sd_backup_result_t *out)
{
    char part_path[BACKUP_PATH_SIZE];
    char final_path[BACKUP_PATH_SIZE];
    memset(out, 0, sizeof *out);
    name_backup(now, reason, out);
    if (!make_backup_dirs(ops, sd_root, out)) {
        return false;
    }
    backup_path(sd_root, out->name, PART_SUFFIX, part_path, sizeof part_path);
    backup_path(sd_root, out->name, "", final_path, sizeof final_path);
    if (exists(final_path)) {
        snprintf(out->error, sizeof out->error, "backup %s already exists", out->display_path);
        return false;
    }
    if (!place_and_verify(ops, part_path, final_path, zip, out)) {
        return false;
    }
    out->pruned = prune(ops, sd_root, keep, out->name);
    return true;
}

bool sd_backup_read(const char *sd_root, const char *name, body_t *out)
{
    char path[BACKUP_PATH_SIZE];
    backup_path(sd_root, name, "", path, sizeof path);
    return read_whole_file(path, out);
}
