#include "save_validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "save_rules.h"

#define MAX_PRINTED_LENGTH 80
#define RATIO_CHECK_FROM_BYTES (4LL * 1024 * 1024)
#define MAX_COMPRESSION_RATIO 200
#define SLOT_COUNT 6
#define META_MAGIC "JKSV"
#define META_TITLE_OFFSET 5
#define META_MIN_LENGTH (META_TITLE_OFFSET + 8)

#define REASON_NOT_A_ZIP "not a zip file"
#define REASON_TOO_MANY "too many entries (max 1000)"
#define REASON_TOO_LARGE "unpacked size over 64 MiB"
#define REASON_SUSPICIOUS "suspicious compression ratio"
#define REASON_NO_OPTION "missing option.sav"
#define REASON_NO_PROGRESS "missing slot_0N/progress.sav"
#define REASON_ANOTHER_GAME "save of another game"

typedef struct {
    zip_entry_t *items;
    int count;
} entry_table_t;

static void set_reason(char *reason, size_t reason_size, const char *text)
{
    snprintf(reason, reason_size, "%s", text);
}

static void set_path_reason(char *reason, size_t reason_size, const char *prefix, const char *path)
{
    char shown[MAX_PRINTED_LENGTH + 1];
    size_t length = 0;
    for (; path[length] != '\0' && length < MAX_PRINTED_LENGTH; length++) {
        unsigned char c = (unsigned char)path[length];
        shown[length] = (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
    }
    shown[length] = '\0';
    snprintf(reason, reason_size, "%s: %s", prefix, shown);
}

static char lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool equals_ignoring_case(const char *left, const char *right)
{
    for (; *left != '\0' && *right != '\0'; left++, right++) {
        if (lower_ascii(*left) != lower_ascii(*right)) {
            return false;
        }
    }
    return *left == *right;
}

/* The first length characters of path equal the whole of other, ignoring ASCII case. */
static bool prefix_equals_ignoring_case(const char *path, size_t length, const char *other)
{
    if (strlen(other) != length) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (lower_ascii(path[i]) != lower_ascii(other[i])) {
            return false;
        }
    }
    return true;
}

static bool load_entries(zip_reader_t *zip, entry_table_t *table)
{
    table->count = zip_reader_count(zip);
    table->items = calloc(table->count > 0 ? (size_t)table->count : 1, sizeof table->items[0]);
    if (table->items == NULL) {
        return false;
    }
    for (int i = 0; i < table->count; i++) {
        zip_reader_entry(zip, i, &table->items[i]);
    }
    return true;
}

static bool size_error(const entry_table_t *table, char *reason, size_t reason_size)
{
    int64_t unpacked = 0;
    int64_t packed = 0;
    for (int i = 0; i < table->count; i++) {
        unpacked += table->items[i].size;
        packed += table->items[i].packed_size;
    }
    if (unpacked > TS_MAX_SAVE_BYTES) {
        set_reason(reason, reason_size, REASON_TOO_LARGE);
        return true;
    }
    if (unpacked >= RATIO_CHECK_FROM_BYTES && unpacked / (packed > 0 ? packed : 1) > MAX_COMPRESSION_RATIO) {
        set_reason(reason, reason_size, REASON_SUSPICIOUS);
        return true;
    }
    return false;
}

static bool entry_error(const entry_table_t *table, char *reason, size_t reason_size)
{
    for (int i = 0; i < table->count; i++) {
        const zip_entry_t *item = &table->items[i];
        if (item->is_name_too_long || !save_rules_is_safe_path(item->path)) {
            set_path_reason(reason, reason_size, "unsafe path", item->path);
            return true;
        }
        if (item->is_symlink) {
            set_path_reason(reason, reason_size, "symlink entry", item->path);
            return true;
        }
    }
    return false;
}

static bool duplicate_error(const entry_table_t *table, char *reason, size_t reason_size)
{
    for (int i = 0; i < table->count; i++) {
        for (int j = 0; j < i; j++) {
            if (equals_ignoring_case(table->items[i].path, table->items[j].path)) {
                set_path_reason(reason, reason_size, "duplicate path", table->items[i].path);
                return true;
            }
        }
    }
    return false;
}

static bool is_file_path(const entry_table_t *table, const char *path, size_t length)
{
    for (int i = 0; i < table->count; i++) {
        if (!table->items[i].is_directory && prefix_equals_ignoring_case(path, length, table->items[i].path)) {
            return true;
        }
    }
    return false;
}

static bool conflict_error(const entry_table_t *table, char *reason, size_t reason_size)
{
    for (int i = 0; i < table->count; i++) {
        const char *path = table->items[i].path;
        for (const char *slash = strchr(path, '/'); slash != NULL; slash = strchr(slash + 1, '/')) {
            if (is_file_path(table, path, (size_t)(slash - path))) {
                set_path_reason(reason, reason_size, "conflicting paths", path);
                return true;
            }
        }
    }
    return false;
}

static bool has_file(const entry_table_t *table, const char *path)
{
    for (int i = 0; i < table->count; i++) {
        if (!table->items[i].is_directory && strcmp(table->items[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

static bool layout_error(const entry_table_t *table, char *reason, size_t reason_size)
{
    if (!has_file(table, "option.sav")) {
        set_reason(reason, reason_size, REASON_NO_OPTION);
        return true;
    }
    for (int slot = 0; slot < SLOT_COUNT; slot++) {
        char progress[32];
        snprintf(progress, sizeof progress, "slot_%02d/progress.sav", slot);
        if (has_file(table, progress)) {
            return false;
        }
    }
    set_reason(reason, reason_size, REASON_NO_PROGRESS);
    return true;
}

static bool structure_error(const entry_table_t *table, char *reason, size_t reason_size)
{
    if (table->count > TS_MAX_SAVE_ENTRIES) {
        set_reason(reason, reason_size, REASON_TOO_MANY);
        return true;
    }
    return size_error(table, reason, reason_size) || entry_error(table, reason, reason_size)
           || duplicate_error(table, reason, reason_size) || conflict_error(table, reason, reason_size)
           || layout_error(table, reason, reason_size);
}

static bool is_meta_for_totk(const unsigned char *data, size_t size)
{
    if (size < META_MIN_LENGTH || memcmp(data, META_MAGIC, strlen(META_MAGIC)) != 0) {
        return false;
    }
    uint64_t title = 0;
    for (int i = 7; i >= 0; i--) {
        title = (title << 8) | data[META_TITLE_OFFSET + i];
    }
    return title == TS_TOTK_TITLE_ID;
}

static bool content_error(zip_reader_t *zip, const entry_table_t *table, bool *is_foreign_meta, char *reason, size_t reason_size)
{
    for (int i = 0; i < table->count; i++) {
        const zip_entry_t *item = &table->items[i];
        if (item->is_directory) {
            continue;
        }
        unsigned char *data = NULL;
        size_t size = 0;
        if (!item->is_supported || !zip_reader_extract(zip, i, &data, &size)) {
            set_path_reason(reason, reason_size, "bad CRC", item->path);
            return true;
        }
        if (save_rules_is_meta(item->path) && !is_meta_for_totk(data, size)) {
            *is_foreign_meta = true;
        }
        free(data);
    }
    return false;
}

static void count_saved(const entry_table_t *table, save_zip_stats_t *stats)
{
    for (int i = 0; i < table->count; i++) {
        const zip_entry_t *item = &table->items[i];
        if (save_rules_is_junk(item->path)) {
            continue;
        }
        if (item->is_directory) {
            stats->directories++;
        } else {
            stats->files++;
            stats->total_bytes += item->size;
        }
    }
}

bool save_validate_zip(zip_reader_t *zip, save_zip_stats_t *stats, char *reason, size_t reason_size)
{
    entry_table_t table = { NULL, 0 };
    bool is_foreign_meta = false;
    memset(stats, 0, sizeof *stats);
    reason[0] = '\0';
    if (zip == NULL || !load_entries(zip, &table)) {
        set_reason(reason, reason_size, REASON_NOT_A_ZIP);
        return false;
    }
    bool is_valid = !structure_error(&table, reason, reason_size)
                    && !content_error(zip, &table, &is_foreign_meta, reason, reason_size);
    if (is_valid && is_foreign_meta) {
        set_reason(reason, reason_size, REASON_ANOTHER_GAME);
        is_valid = false;
    }
    if (is_valid) {
        count_saved(&table, stats);
    }
    free(table.items);
    return is_valid;
}
