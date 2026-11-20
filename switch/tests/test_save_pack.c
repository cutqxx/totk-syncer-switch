#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <share.h>
#else
#include <sys/stat.h>
#endif

#include "save_pack.h"
#include "test.h"
#include "test_fs.h"
#include "test_support.h"
#include "test_zip_read.h"

#define SAVE_TREE_DIR CONTRACTS_DIR "/save-tree"
#define SWITCH_ZIP_FILE CONTRACTS_DIR "/save-zip/valid-from-switch.zip"
#define REGENERATE_VARIABLE "TS_REGENERATE_CONTRACT_ZIP"
#define BIG_LIMIT (64LL * 1024 * 1024)

/* The fixed stamp of the contract archive (the same as FIXED_TIME in contracts/save-zip/generate.py). */
static struct tm contract_time(void)
{
    struct tm when;
    memset(&when, 0, sizeof when);
    when.tm_year = 126;
    when.tm_mon = 10;
    when.tm_mday = 3;
    return when;
}

static pack_status_t pack_with_time(const char *root, const struct tm *when, int64_t limit, body_t *zip, pack_stats_t *stats)
{
    memset(zip, 0, sizeof *zip);
    return save_pack_dir(root, when, limit, zip, stats);
}

static pack_status_t pack(const char *root, int64_t limit, body_t *zip, pack_stats_t *stats)
{
    struct tm when = contract_time();
    return pack_with_time(root, &when, limit, zip, stats);
}

static bool open_zip(const body_t *zip, test_zip_t *listing)
{
    memset(listing, 0, sizeof *listing);
    return zip->data != NULL && test_zip_open(zip->data, (size_t)zip->size, listing);
}

static unsigned char *read_file_exactly(const char *path, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *content = malloc((size_t)size + 1);
    if (content != NULL && fread(content, 1, (size_t)size, file) != (size_t)size) {
        free(content);
        content = NULL;
    }
    fclose(file);
    *size_out = (size_t)size;
    return content;
}

static bool equals_ignoring_case(const char *left, const char *right)
{
    for (; *left != '\0' && *right != '\0'; left++, right++) {
        char a = (*left >= 'A' && *left <= 'Z') ? (char)(*left + 32) : *left;
        char b = (*right >= 'A' && *right <= 'Z') ? (char)(*right + 32) : *right;
        if (a != b) {
            return false;
        }
    }
    return *left == *right;
}

/* A file the app cannot open: held open without sharing (Windows) or stripped of permissions (elsewhere). */
typedef struct {
    FILE *held;
    char path[512];
} locked_file_t;

static void lock_file(locked_file_t *lock, const char *root, const char *rel)
{
    snprintf(lock->path, sizeof lock->path, "%s/%s", root, rel);
#ifdef _WIN32
    lock->held = _fsopen(lock->path, "rb", _SH_DENYRW);
    CHECK(lock->held != NULL);
#else
    lock->held = NULL;
    CHECK(chmod(lock->path, 0) == 0);
#endif
}

static void unlock_file(locked_file_t *lock)
{
    if (lock->held != NULL) {
        fclose(lock->held);
    }
#ifndef _WIN32
    chmod(lock->path, 0644);
#endif
}

static void write_numbered_files(const char *root, int count)
{
    for (int i = 0; i < count; i++) {
        char name[32];
        snprintf(name, sizeof name, "f%04d.sav", i);
        CHECK(test_fs_write_file(root, name, "x", 1));
    }
}

static void make_numbered_directories(const char *root, int count)
{
    for (int i = 0; i < count; i++) {
        char name[32];
        snprintf(name, sizeof name, "d%04d", i);
        CHECK(test_fs_make_dir(root, name));
    }
}

/* ---- contract: the Switch ZIP of save-tree is what the PC expects ---- */

typedef struct {
    char root[256];
    body_t zip;
    pack_stats_t stats;
    test_zip_t listing;
    cJSON *cases;
    const cJSON *expected;
} contract_pack_t;

static bool contract_pack_open(contract_pack_t *contract)
{
    memset(contract, 0, sizeof *contract);
    bool is_tree_ready = test_fs_make_contract_save(contract->root, sizeof contract->root);
    pack_status_t status = is_tree_ready ? pack(contract->root, BIG_LIMIT, &contract->zip, &contract->stats) : PACK_ERR_READ;
    contract->cases = test_load_cases();
    contract->expected = contract->cases != NULL ? test_find_case(contract->cases, "valid-save-tree.zip") : NULL;
    bool is_ready = status == PACK_OK && open_zip(&contract->zip, &contract->listing) && contract->expected != NULL;
    CHECK(is_ready);
    return is_ready;
}

static void contract_pack_close(contract_pack_t *contract)
{
    test_zip_close(&contract->listing);
    body_free(&contract->zip);
    cJSON_Delete(contract->cases);
}

static const cJSON *expected_array(const contract_pack_t *contract, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(contract->expected, name);
}

static void the_zip_holds_the_contract_directories_first_then_the_contract_files_in_byte_order(void)
{
    contract_pack_t contract;
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    const cJSON *dirs = expected_array(&contract, "saved_dirs");
    const cJSON *files = expected_array(&contract, "saved_files");
    int dir_count = cJSON_GetArraySize(dirs);
    int file_count = cJSON_GetArraySize(files);
    CHECK_INT_EQ(contract.listing.count, dir_count + file_count);
    for (int i = 0; i < dir_count && i < contract.listing.count; i++) {
        char expected_name[TEST_ZIP_NAME_SIZE];
        snprintf(expected_name, sizeof expected_name, "%s/", cJSON_GetArrayItem(dirs, i)->valuestring);
        CHECK_STR_EQ(contract.listing.items[i].name, expected_name);
        CHECK(contract.listing.items[i].is_directory);
    }
    for (int i = 0; i < file_count && dir_count + i < contract.listing.count; i++) {
        CHECK_STR_EQ(contract.listing.items[dir_count + i].name, cJSON_GetArrayItem(files, i)->valuestring);
        CHECK(!contract.listing.items[dir_count + i].is_directory);
    }
    contract_pack_close(&contract);
}

static void every_file_in_the_zip_has_the_bytes_of_the_save_file(void)
{
    contract_pack_t contract;
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    const cJSON *name = NULL;
    cJSON_ArrayForEach(name, expected_array(&contract, "saved_files"))
    {
        char path[512];
        size_t source_size = 0;
        snprintf(path, sizeof path, "%s/%s", SAVE_TREE_DIR, name->valuestring);
        unsigned char *source = read_file_exactly(path, &source_size);
        const test_zip_item_t *item = test_zip_find(&contract.listing, name->valuestring);
        CHECK(source != NULL);
        CHECK(item != NULL);
        if (source != NULL && item != NULL) {
            CHECK_INT_EQ(item->size, source_size);
            CHECK(source_size == 0 || memcmp(item->data, source, source_size) == 0);
        }
        free(source);
    }
    contract_pack_close(&contract);
}

static void the_zip_has_no_junk_and_no_meta_file(void)
{
    static const char *const junk[] = { ".nx_save_meta.bin", "desktop.ini", "Thumbs.db", "x.zip", "THUMBS.DB" };
    contract_pack_t contract;
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    for (int i = 0; i < contract.listing.count; i++) {
        const char *name = contract.listing.items[i].name;
        const char *slash = strrchr(name, '/');
        const char *base = slash != NULL ? slash + 1 : name;
        for (size_t j = 0; j < sizeof junk / sizeof junk[0]; j++) {
            CHECK(!equals_ignoring_case(base, junk[j]));
        }
    }
    contract_pack_close(&contract);
}

static void entry_names_use_forward_slashes_and_are_relative(void)
{
    contract_pack_t contract;
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    for (int i = 0; i < contract.listing.count; i++) {
        const char *name = contract.listing.items[i].name;
        CHECK(strchr(name, '\\') == NULL);
        CHECK(name[0] != '/');
        CHECK(strchr(name, ':') == NULL);
        CHECK(strstr(name, "..") == NULL);
        CHECK(strstr(name, "//") == NULL);
    }
    CHECK(test_zip_find(&contract.listing, "option.sav") != NULL);
    CHECK(test_zip_find(&contract.listing, "slot_00/progress.sav") != NULL);
    CHECK(test_zip_find(&contract.listing, "picturebook/a/b.dat") != NULL);
    contract_pack_close(&contract);
}

static void the_empty_album_folder_is_in_the_zip_as_an_entry(void)
{
    contract_pack_t contract;
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    const test_zip_item_t *album = test_zip_find(&contract.listing, "album/");
    CHECK(album != NULL);
    if (album != NULL) {
        CHECK(album->is_directory);
        CHECK_INT_EQ(album->size, 0);
    }
    contract_pack_close(&contract);
}

static void the_fingerprint_of_the_zip_equals_the_contract_fingerprint(void)
{
    contract_pack_t contract;
    char fingerprint[80];
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    char *expected = test_read_file(CONTRACTS_DIR "/save-tree.fingerprint.txt");
    CHECK(expected != NULL);
    if (expected != NULL) {
        size_t length = strlen(expected);
        while (length > 0 && (expected[length - 1] == '\n' || expected[length - 1] == '\r' || expected[length - 1] == ' ')) {
            expected[--length] = '\0';
        }
        test_zip_fingerprint(&contract.listing, fingerprint);
        CHECK_STR_EQ(fingerprint, expected);
    }
    free(expected);
    contract_pack_close(&contract);
}

static void the_stats_describe_what_was_packed(void)
{
    contract_pack_t contract;
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    int64_t unpacked = 0;
    for (int i = 0; i < contract.listing.count; i++) {
        unpacked += (int64_t)contract.listing.items[i].size;
    }
    CHECK_INT_EQ(contract.stats.files, cJSON_GetArraySize(expected_array(&contract, "saved_files")));
    CHECK_INT_EQ(contract.stats.directories, cJSON_GetArraySize(expected_array(&contract, "saved_dirs")));
    CHECK_INT_EQ(contract.stats.total_bytes, unpacked);
    CHECK_INT_EQ(contract.stats.zip_bytes, contract.zip.size);
    CHECK(contract.stats.problem_path[0] == '\0');
    contract_pack_close(&contract);
}

static void packing_twice_gives_the_same_bytes(void)
{
    contract_pack_t contract;
    body_t again;
    pack_stats_t again_stats;
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    CHECK_INT_EQ(pack(contract.root, BIG_LIMIT, &again, &again_stats), PACK_OK);
    CHECK_INT_EQ(again.size, contract.zip.size);
    CHECK(again.size == contract.zip.size && memcmp(again.data, contract.zip.data, (size_t)again.size) == 0);
    body_free(&again);
    contract_pack_close(&contract);
}

static void the_same_save_in_another_folder_gives_the_same_bytes(void)
{
    contract_pack_t contract;
    char other_root[256];
    body_t other;
    pack_stats_t other_stats;
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    CHECK(test_fs_make_contract_save(other_root, sizeof other_root));
    CHECK_INT_EQ(pack(other_root, BIG_LIMIT, &other, &other_stats), PACK_OK);
    CHECK_INT_EQ(other.size, contract.zip.size);
    CHECK(other.size == contract.zip.size && memcmp(other.data, contract.zip.data, (size_t)other.size) == 0);
    body_free(&other);
    contract_pack_close(&contract);
}

static void every_entry_carries_the_timestamp_passed_in(void)
{
    contract_pack_t contract;
    struct tm expected_tm = contract_time();
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    expected_tm.tm_isdst = -1;
    time_t expected = mktime(&expected_tm);
    for (int i = 0; i < contract.listing.count; i++) {
        CHECK_INT_EQ((long long)contract.listing.items[i].modified, (long long)expected);
    }
    contract_pack_close(&contract);
}

static void packing_never_changes_the_save_tree(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_contract_save(root, sizeof root));
    char *before = test_fs_snapshot(root);
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_OK);
    char *after = test_fs_snapshot(root);
    CHECK(before != NULL && after != NULL);
    if (before != NULL && after != NULL) {
        CHECK_STR_EQ(after, before);
        CHECK(strstr(before, "album|dir") != NULL);
    }
    free(before);
    free(after);
    body_free(&zip);
}

static void the_committed_switch_zip_lists_the_same_entries_as_a_fresh_pack(void)
{
    contract_pack_t contract;
    size_t committed_size = 0;
    test_zip_t committed;
    if (!contract_pack_open(&contract)) {
        contract_pack_close(&contract);
        return;
    }
    if (getenv(REGENERATE_VARIABLE) != NULL) {
        FILE *file = fopen(SWITCH_ZIP_FILE, "wb");
        CHECK(file != NULL);
        if (file != NULL) {
            CHECK(fwrite(contract.zip.data, 1, (size_t)contract.zip.size, file) == (size_t)contract.zip.size);
            fclose(file);
        }
    }
    unsigned char *bytes = read_file_exactly(SWITCH_ZIP_FILE, &committed_size);
    CHECK(bytes != NULL);
    if (bytes != NULL && test_zip_open(bytes, committed_size, &committed)) {
        CHECK_INT_EQ(committed.count, contract.listing.count);
        for (int i = 0; i < committed.count && i < contract.listing.count; i++) {
            CHECK_STR_EQ(committed.items[i].name, contract.listing.items[i].name);
            CHECK_INT_EQ(committed.items[i].is_directory, contract.listing.items[i].is_directory);
            CHECK_INT_EQ(committed.items[i].crc, contract.listing.items[i].crc);
            CHECK_INT_EQ(committed.items[i].size, contract.listing.items[i].size);
        }
        test_zip_close(&committed);
    } else {
        CHECK(bytes == NULL);
        printf("  %s is missing or unreadable; run the tests once with %s=1 to create it\n", SWITCH_ZIP_FILE, REGENERATE_VARIABLE);
    }
    free(bytes);
    contract_pack_close(&contract);
}

/* ---- other saves: shapes the contract tree does not have ---- */

static void a_save_with_an_empty_file_and_a_large_incompressible_file_round_trips(void)
{
    static unsigned char noise[200 * 1024];
    char root[256];
    body_t zip;
    pack_stats_t stats;
    test_zip_t listing;
    unsigned int seed = 99;
    for (size_t i = 0; i < sizeof noise; i++) {
        seed = seed * 1103515245u + 12345u;
        noise[i] = (unsigned char)(seed >> 16);
    }
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "empty.sav", "", 0));
    CHECK(test_fs_write_file(root, "dir/noise.bin", noise, sizeof noise));
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_OK);
    bool is_listed = open_zip(&zip, &listing);
    CHECK(is_listed);
    if (is_listed) {
        CHECK_INT_EQ(stats.files, 2);
        CHECK_INT_EQ(stats.directories, 1);
        CHECK_INT_EQ(stats.total_bytes, sizeof noise);
        CHECK_INT_EQ(test_zip_find(&listing, "empty.sav")->size, 0);
        CHECK_INT_EQ(test_zip_find(&listing, "dir/noise.bin")->size, sizeof noise);
        CHECK(memcmp(test_zip_find(&listing, "dir/noise.bin")->data, noise, sizeof noise) == 0);
        test_zip_close(&listing);
    }
    body_free(&zip);
}

static void an_empty_save_gives_a_valid_zip_without_entries(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    test_zip_t listing;
    CHECK(test_fs_make_root(root, sizeof root));
    memset(&stats, 0xFF, sizeof stats);
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_OK);
    bool is_listed = open_zip(&zip, &listing);
    CHECK(is_listed);
    if (is_listed) {
        CHECK_INT_EQ(listing.count, 0);
        CHECK_INT_EQ(stats.files, 0);
        CHECK_INT_EQ(stats.directories, 0);
        CHECK_INT_EQ(stats.total_bytes, 0);
        CHECK_INT_EQ(stats.zip_bytes, zip.size);
        CHECK(stats.problem_path[0] == '\0');
        test_zip_close(&listing);
    }
    body_free(&zip);
}

static void a_save_with_only_junk_packs_like_an_empty_one(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    test_zip_t listing;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "desktop.ini", "x", 1));
    CHECK(test_fs_write_file(root, "Thumbs.db", "x", 1));
    CHECK(test_fs_write_file(root, ".nx_save_meta.bin", "x", 1));
    CHECK(test_fs_write_file(root, "backup.ZIP", "x", 1));
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_OK);
    bool is_listed = open_zip(&zip, &listing);
    CHECK(is_listed);
    if (is_listed) {
        CHECK_INT_EQ(listing.count, 0);
        CHECK_INT_EQ(stats.files, 0);
        test_zip_close(&listing);
    }
    body_free(&zip);
}

static void deeply_nested_folders_are_all_listed_before_the_file_inside(void)
{
    enum { LEVELS = 100 };
    char root[256];
    char rel[2 * LEVELS + 16];
    char expected_deepest[2 * LEVELS + 32];
    body_t zip;
    pack_stats_t stats;
    test_zip_t listing;
    CHECK(test_fs_make_root(root, sizeof root));
    rel[0] = '\0';
    for (int i = 0; i < LEVELS; i++) {
        strcat(rel, i == 0 ? "d" : "/d");
    }
    snprintf(expected_deepest, sizeof expected_deepest, "%s/", rel);
    strcat(rel, "/deep.sav");
    CHECK(test_fs_write_file(root, rel, "deep", 4));
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_OK);
    bool is_listed = open_zip(&zip, &listing);
    CHECK(is_listed);
    if (is_listed) {
        CHECK_INT_EQ(stats.directories, LEVELS);
        CHECK_INT_EQ(listing.count, LEVELS + 1);
        if (listing.count == LEVELS + 1) {
            CHECK_STR_EQ(listing.items[0].name, "d/");
            CHECK_STR_EQ(listing.items[LEVELS - 1].name, expected_deepest);
            CHECK_STR_EQ(listing.items[LEVELS].name, rel);
            CHECK(memcmp(listing.items[LEVELS].data, "deep", 4) == 0);
        }
        test_zip_close(&listing);
    }
    body_free(&zip);
}

/* ---- refusals: nothing is built ---- */

static void a_save_above_the_limit_is_too_large_and_reports_its_size(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "a.sav", "0123456789", 10));
    CHECK(test_fs_write_file(root, "dir/b.sav", "0123456789", 10));
    CHECK_INT_EQ(pack(root, 19, &zip, &stats), PACK_ERR_TOO_LARGE);
    CHECK_INT_EQ(stats.total_bytes, 20);
    CHECK(zip.data == NULL);
    CHECK_INT_EQ(zip.size, 0);
    CHECK_INT_EQ(stats.zip_bytes, 0);
}

static void a_save_exactly_at_the_limit_is_accepted(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "a.sav", "0123456789", 10));
    CHECK(test_fs_write_file(root, "dir/b.sav", "0123456789", 10));
    CHECK_INT_EQ(pack(root, 20, &zip, &stats), PACK_OK);
    CHECK(zip.size > 0);
    body_free(&zip);
}

static void junk_does_not_count_towards_the_size_limit(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "a.sav", "0123456789", 10));
    CHECK(test_fs_write_file(root, "thumbs.db", "0123456789", 10));
    CHECK_INT_EQ(pack(root, 10, &zip, &stats), PACK_OK);
    CHECK_INT_EQ(stats.total_bytes, 10);
    body_free(&zip);
}

static void the_size_is_judged_before_any_file_is_read(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    locked_file_t lock;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "a.sav", "0123456789", 10));
    lock_file(&lock, root, "a.sav");
    CHECK_INT_EQ(pack(root, 5, &zip, &stats), PACK_ERR_TOO_LARGE);
    unlock_file(&lock);
}

static void one_thousand_files_and_folders_are_accepted(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_root(root, sizeof root));
    write_numbered_files(root, 500);
    make_numbered_directories(root, 500);
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_OK);
    CHECK_INT_EQ(stats.files + stats.directories, 1000);
    body_free(&zip);
}

static void the_one_thousand_and_first_entry_is_too_many(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_root(root, sizeof root));
    write_numbered_files(root, 501);
    make_numbered_directories(root, 500);
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_ERR_TOO_MANY_ENTRIES);
    CHECK(zip.data == NULL);
    CHECK(stats.problem_path[0] != '\0');
}

static void junk_files_do_not_count_towards_the_entry_limit(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_root(root, sizeof root));
    write_numbered_files(root, 1000);
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof name, "junk%d.zip", i);
        CHECK(test_fs_write_file(root, name, "x", 1));
    }
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_OK);
    CHECK_INT_EQ(stats.files, 1000);
    body_free(&zip);
}

static void a_non_ascii_file_name_is_refused_and_named(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "option.sav", "x", 1));
    CHECK(test_fs_write_file(root, "caf\xE9.sav", "x", 1));
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_ERR_BAD_NAME);
    CHECK(strncmp(stats.problem_path, "caf", 3) == 0);
    CHECK(zip.data == NULL);
}

static void a_bad_name_inside_a_folder_is_reported_with_its_folder(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "slot_00/progress.sav", "x", 1));
    CHECK(test_fs_write_file(root, "slot_00/\xE9.sav", "x", 1));
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_ERR_BAD_NAME);
    CHECK(strncmp(stats.problem_path, "slot_00/", 8) == 0);
    CHECK(strlen(stats.problem_path) > 8);
}

static void a_non_ascii_folder_name_is_refused_too(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "\xE9/progress.sav", "x", 1));
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_ERR_BAD_NAME);
}

static void a_file_that_cannot_be_read_stops_the_pack_and_is_named(void)
{
    char root[256];
    body_t zip;
    pack_stats_t stats;
    locked_file_t lock;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "option.sav", "x", 1));
    CHECK(test_fs_write_file(root, "slot_00/progress.sav", "progress", 8));
    lock_file(&lock, root, "slot_00/progress.sav");
    CHECK_INT_EQ(pack(root, BIG_LIMIT, &zip, &stats), PACK_ERR_READ);
    CHECK_STR_EQ(stats.problem_path, "slot_00/progress.sav");
    CHECK(zip.data == NULL);
    CHECK_INT_EQ(zip.size, 0);
    unlock_file(&lock);
}

static void a_missing_save_root_is_a_read_failure(void)
{
    body_t zip;
    pack_stats_t stats;
    CHECK_INT_EQ(pack("build/tmp/no-such-save", BIG_LIMIT, &zip, &stats), PACK_ERR_READ);
    CHECK(zip.data == NULL);
}

void run_save_pack_tests(void)
{
    RUN_TEST(the_zip_holds_the_contract_directories_first_then_the_contract_files_in_byte_order);
    RUN_TEST(every_file_in_the_zip_has_the_bytes_of_the_save_file);
    RUN_TEST(the_zip_has_no_junk_and_no_meta_file);
    RUN_TEST(entry_names_use_forward_slashes_and_are_relative);
    RUN_TEST(the_empty_album_folder_is_in_the_zip_as_an_entry);
    RUN_TEST(the_fingerprint_of_the_zip_equals_the_contract_fingerprint);
    RUN_TEST(the_stats_describe_what_was_packed);
    RUN_TEST(packing_twice_gives_the_same_bytes);
    RUN_TEST(the_same_save_in_another_folder_gives_the_same_bytes);
    RUN_TEST(every_entry_carries_the_timestamp_passed_in);
    RUN_TEST(packing_never_changes_the_save_tree);
    RUN_TEST(the_committed_switch_zip_lists_the_same_entries_as_a_fresh_pack);
    RUN_TEST(a_save_with_an_empty_file_and_a_large_incompressible_file_round_trips);
    RUN_TEST(an_empty_save_gives_a_valid_zip_without_entries);
    RUN_TEST(a_save_with_only_junk_packs_like_an_empty_one);
    RUN_TEST(deeply_nested_folders_are_all_listed_before_the_file_inside);
    RUN_TEST(a_save_above_the_limit_is_too_large_and_reports_its_size);
    RUN_TEST(a_save_exactly_at_the_limit_is_accepted);
    RUN_TEST(junk_does_not_count_towards_the_size_limit);
    RUN_TEST(the_size_is_judged_before_any_file_is_read);
    RUN_TEST(one_thousand_files_and_folders_are_accepted);
    RUN_TEST(the_one_thousand_and_first_entry_is_too_many);
    RUN_TEST(junk_files_do_not_count_towards_the_entry_limit);
    RUN_TEST(a_non_ascii_file_name_is_refused_and_named);
    RUN_TEST(a_bad_name_inside_a_folder_is_reported_with_its_folder);
    RUN_TEST(a_non_ascii_folder_name_is_refused_too);
    RUN_TEST(a_file_that_cannot_be_read_stops_the_pack_and_is_named);
    RUN_TEST(a_missing_save_root_is_a_read_failure);
}
