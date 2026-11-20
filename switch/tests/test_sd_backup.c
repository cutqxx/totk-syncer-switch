#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "backup_to_sd.h"
#include "save_pack.h"
#include "sd_backup.h"
#include "test.h"
#include "test_fs.h"
#include "test_support.h"
#include "test_zip_read.h"

#define BACKUPS_REL "switch/totk-sync/backups"
#define BIG_LIMIT (64LL * 1024 * 1024)

static struct tm at(int day, int hour, int minute, int second)
{
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = 126;
    t.tm_mon = 10;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    return t;
}

static body_t contract_zip(void)
{
    char save[256];
    body_t zip = { NULL, 0 };
    pack_stats_t stats;
    struct tm when = at(3, 0, 0, 0);
    CHECK(test_fs_make_contract_save(save, sizeof save));
    CHECK_INT_EQ(save_pack_dir(save, &when, BIG_LIMIT, &zip, &stats), PACK_OK);
    return zip;
}

static bool file_exists(const char *root, const char *rel)
{
    char path[512];
    struct stat info;
    snprintf(path, sizeof path, "%s/%s", root, rel);
    return stat(path, &info) == 0;
}

static int count_files_in(const char *root, const char *rel)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    DIR *dir = opendir(path);
    int count = 0;
    for (struct dirent *item = dir ? readdir(dir) : NULL; item != NULL; item = readdir(dir)) {
        count += item->d_name[0] != '.' ? 1 : 0;
    }
    if (dir != NULL) {
        closedir(dir);
    }
    return count;
}

static void a_backup_is_named_by_time_and_reason_and_holds_the_exact_zip(void)
{
    char sd[256];
    body_t zip = contract_zip();
    sd_backup_result_t result;
    struct tm when = at(7, 9, 5, 3);
    CHECK(test_fs_make_root(sd, sizeof sd));

    CHECK(sd_backup_write(file_ops_stdio(), sd, &when, SD_BACKUP_MANUAL, &zip, 20, &result));

    CHECK_STR_EQ(result.name, "totk-20261107-090503-manual.zip");
    CHECK_STR_EQ(result.display_path, "sd:/switch/totk-sync/backups/totk-20261107-090503-manual.zip");
    body_t copy = { NULL, 0 };
    CHECK(sd_backup_read(sd, result.name, &copy));
    CHECK(copy.size == zip.size && memcmp(copy.data, zip.data, (size_t)zip.size) == 0);
    CHECK(!file_exists(sd, BACKUPS_REL "/totk-20261107-090503-manual.zip.part"));
    body_free(&copy);
    body_free(&zip);
}

static void a_before_pull_backup_carries_its_reason_in_the_name(void)
{
    char sd[256];
    body_t zip = contract_zip();
    sd_backup_result_t result;
    struct tm when = at(7, 23, 59, 59);
    CHECK(test_fs_make_root(sd, sizeof sd));

    CHECK(sd_backup_write(file_ops_stdio(), sd, &when, SD_BACKUP_BEFORE_PULL, &zip, 20, &result));

    CHECK_STR_EQ(result.name, "totk-20261107-235959-before-pull.zip");
    CHECK(file_exists(sd, BACKUPS_REL "/totk-20261107-235959-before-pull.zip"));
    body_free(&zip);
}

static void a_write_failure_reports_the_reason_and_leaves_no_part_file(void)
{
    static const fault_step_t steps[] = { FAULT_MAKE_DIR, FAULT_OPEN, FAULT_WRITE, FAULT_CLOSE, FAULT_RENAME };
    body_t zip = contract_zip();
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        char sd[256];
        faulty_files_t files;
        sd_backup_result_t result;
        struct tm when = at(7, 10, 0, 0);
        CHECK(test_fs_make_root(sd, sizeof sd));
        faulty_files_init(&files);
        files.fail_step = steps[i];
        files.fail_at = 1;

        CHECK(!sd_backup_write(&files.ops, sd, &when, SD_BACKUP_MANUAL, &zip, 20, &result));

        CHECK(strstr(result.error, "No space left on device") != NULL);
        CHECK(!file_exists(sd, BACKUPS_REL "/totk-20261107-100000-manual.zip.part"));
        CHECK(!file_exists(sd, BACKUPS_REL "/totk-20261107-100000-manual.zip"));
        CHECK(test_is_ascii_text(result.error));
    }
    body_free(&zip);
}

static void a_backup_that_does_not_read_back_is_removed_and_reported(void)
{
    char sd[256];
    body_t broken = { NULL, 0 };
    sd_backup_result_t result;
    struct tm when = at(7, 11, 0, 0);
    CHECK(test_fs_make_root(sd, sizeof sd));
    CHECK(body_append(&broken, "PK\x03\x04 not really a zip", 23, 1024));

    CHECK(!sd_backup_write(file_ops_stdio(), sd, &when, SD_BACKUP_MANUAL, &broken, 20, &result));

    CHECK(strstr(result.error, "does not read back correctly") != NULL);
    CHECK(!file_exists(sd, BACKUPS_REL "/totk-20261107-110000-manual.zip"));
    body_free(&broken);
}

static void an_existing_backup_with_the_same_name_is_never_overwritten(void)
{
    char sd[256];
    body_t zip = contract_zip();
    sd_backup_result_t result;
    struct tm when = at(7, 12, 0, 0);
    CHECK(test_fs_make_root(sd, sizeof sd));
    CHECK(test_fs_write_file(sd, BACKUPS_REL "/totk-20261107-120000-manual.zip", "keep", 4));

    CHECK(!sd_backup_write(file_ops_stdio(), sd, &when, SD_BACKUP_MANUAL, &zip, 20, &result));

    body_t kept = { NULL, 0 };
    CHECK(sd_backup_read(sd, "totk-20261107-120000-manual.zip", &kept));
    CHECK_INT_EQ(kept.size, 4);
    body_free(&kept);
    body_free(&zip);
}

static void pruning_keeps_the_newest_own_backups_and_never_touches_other_files(void)
{
    char sd[256];
    body_t zip = contract_zip();
    sd_backup_result_t result;
    CHECK(test_fs_make_root(sd, sizeof sd));
    for (int i = 0; i < 22; i++) {
        char rel[128];
        snprintf(rel, sizeof rel, BACKUPS_REL "/totk-202611%02d-080000-%s.zip", i + 1, i % 2 ? "manual" : "before-pull");
        CHECK(test_fs_write_file(sd, rel, "old", 3));
    }
    CHECK(test_fs_write_file(sd, BACKUPS_REL "/notes.txt", "mine", 4));
    CHECK(test_fs_write_file(sd, BACKUPS_REL "/totk-20261101-080000-other.zip", "mine", 4));
    CHECK(test_fs_write_file(sd, BACKUPS_REL "/JKSV-totk.zip", "mine", 4));
    CHECK(test_fs_write_file(sd, BACKUPS_REL "/totk-20261101-070000-manual.zip.part", "part", 4));
    struct tm when = at(30, 9, 0, 0);

    CHECK(sd_backup_write(file_ops_stdio(), sd, &when, SD_BACKUP_MANUAL, &zip, 20, &result));

    CHECK_INT_EQ(result.pruned, 3);
    CHECK(file_exists(sd, BACKUPS_REL "/totk-20261130-090000-manual.zip"));
    CHECK(!file_exists(sd, BACKUPS_REL "/totk-20261101-080000-before-pull.zip"));
    CHECK(!file_exists(sd, BACKUPS_REL "/totk-20261103-080000-before-pull.zip"));
    CHECK(file_exists(sd, BACKUPS_REL "/totk-20261104-080000-manual.zip"));
    CHECK(file_exists(sd, BACKUPS_REL "/notes.txt"));
    CHECK(file_exists(sd, BACKUPS_REL "/totk-20261101-080000-other.zip"));
    CHECK(file_exists(sd, BACKUPS_REL "/JKSV-totk.zip"));
    CHECK(file_exists(sd, BACKUPS_REL "/totk-20261101-070000-manual.zip.part"));
    CHECK_INT_EQ(count_files_in(sd, BACKUPS_REL), 20 + 4);
    body_free(&zip);
}

static void the_new_backup_survives_even_when_older_names_sort_after_it(void)
{
    char sd[256];
    body_t zip = contract_zip();
    sd_backup_result_t result;
    CHECK(test_fs_make_root(sd, sizeof sd));
    CHECK(test_fs_write_file(sd, BACKUPS_REL "/totk-20991231-235959-manual.zip", "future", 6));
    struct tm when = at(7, 9, 0, 0);

    CHECK(sd_backup_write(file_ops_stdio(), sd, &when, SD_BACKUP_MANUAL, &zip, 1, &result));

    CHECK(file_exists(sd, BACKUPS_REL "/totk-20261107-090000-manual.zip"));
    CHECK(file_exists(sd, BACKUPS_REL "/totk-20991231-235959-manual.zip"));
    body_free(&zip);
}

static void own_names_follow_the_template_exactly(void)
{
    CHECK(sd_backup_is_own_name("totk-20261107-090503-manual.zip"));
    CHECK(sd_backup_is_own_name("totk-20261107-090503-before-pull.zip"));
    CHECK(!sd_backup_is_own_name("totk-20261107-090503-other.zip"));
    CHECK(!sd_backup_is_own_name("totk-2026110-0905033-manual.zip"));
    CHECK(!sd_backup_is_own_name("totk-20261107-090503-manual.zip.part"));
    CHECK(!sd_backup_is_own_name("TOTK-20261107-090503-manual.zip"));
    CHECK(!sd_backup_is_own_name("totk-2026II07-090503-manual.zip"));
}

/* The "Y - Backup to SD" scenario. */
typedef struct {
    fake_world_t world;
    fake_console_t console;
    config_t cfg;
    char save[256];
    char sd[256];
    backup_result_t result;
} backup_fixture_t;

static void backup_fixture_open(backup_fixture_t *f)
{
    memset(f, 0, sizeof *f);
    fake_world_init(&f->world, NULL, 0);
    fake_console_init(&f->console, &f->world);
    test_make_config(&f->cfg);
    CHECK(test_fs_make_contract_save(f->save, sizeof f->save));
    CHECK(test_fs_make_root(f->sd, sizeof f->sd));
    snprintf(f->console.mount_root, sizeof f->console.mount_root, "%s", f->save);
    f->world.env.sd_root = f->sd;
}

static void backup_to_sd_writes_the_save_zip_and_reports_its_path(void)
{
    backup_fixture_t f;
    backup_fixture_open(&f);
    char *before = test_fs_snapshot(f.save);

    backup_save_to_sd(&f.world.env, &f.cfg, BIG_LIMIT, &f.result);

    CHECK_INT_EQ(f.result.kind, BACKUP_OK);
    CHECK_STR_EQ(f.result.message, "BACKUP OK: sd:/switch/totk-sync/backups/totk-20261107-120000-manual.zip");
    body_t written = { NULL, 0 };
    CHECK(sd_backup_read(f.sd, f.result.name, &written));
    test_zip_t listing;
    CHECK(test_zip_open(written.data, (size_t)written.size, &listing));
    char fingerprint[80];
    test_zip_fingerprint(&listing, fingerprint);
    char *expected = test_read_file(CONTRACTS_DIR "/save-tree.fingerprint.txt");
    CHECK(expected != NULL && strncmp(fingerprint, expected, strlen(fingerprint)) == 0);
    char *after = test_fs_snapshot(f.save);
    CHECK(before != NULL && after != NULL && strcmp(before, after) == 0);
    CHECK_INT_EQ(f.console.rw_mount_calls, 0);
    CHECK_INT_EQ(f.console.commit_calls, 0);
    CHECK_INT_EQ(f.console.unmount_calls, 1);
    CHECK_INT_EQ(f.world.net_calls, 0);
    test_zip_close(&listing);
    body_free(&written);
    free(expected);
    free(before);
    free(after);
}

static void backup_to_sd_refuses_while_totk_runs_and_mounts_nothing(void)
{
    backup_fixture_t f;
    backup_fixture_open(&f);
    f.console.probe.is_found = true;
    f.console.probe.pid = 5;

    backup_save_to_sd(&f.world.env, &f.cfg, BIG_LIMIT, &f.result);

    CHECK_INT_EQ(f.result.kind, BACKUP_GAME_RUNNING);
    CHECK_STR_EQ(f.result.message, "Close TotK first (HOME -> close the game)");
    CHECK_INT_EQ(f.console.mount_calls, 0);
    CHECK_INT_EQ(count_files_in(f.sd, BACKUPS_REL), 0);
}

static void backup_to_sd_reports_an_sd_failure_with_its_reason(void)
{
    backup_fixture_t f;
    faulty_files_t files;
    backup_fixture_open(&f);
    faulty_files_init(&files);
    files.fail_step = FAULT_WRITE;
    files.fail_at = 1;
    f.world.env.files = &files.ops;

    backup_save_to_sd(&f.world.env, &f.cfg, BIG_LIMIT, &f.result);

    CHECK_INT_EQ(f.result.kind, BACKUP_SD_FAILED);
    CHECK(strncmp(f.result.message, "BACKUP FAILED: cannot write sd:/switch/totk-sync/backups/", 57) == 0);
    CHECK(strstr(f.result.message, "No space left on device. No backup was made") != NULL);
    CHECK_INT_EQ(count_files_in(f.sd, BACKUPS_REL), 0);
}

static void backup_to_sd_uses_its_own_account_list_title(void)
{
    backup_fixture_t f;
    static const uint64_t uid[2] = { 9, 9 };
    backup_fixture_open(&f);
    account_make(&f.console.accounts[1], uid, "Second", 2);
    f.console.account_count = 2;
    f.world.choose_result = -1;

    backup_save_to_sd(&f.world.env, &f.cfg, BIG_LIMIT, &f.result);

    CHECK_INT_EQ(f.result.kind, BACKUP_CANCELLED);
    CHECK_STR_EQ(f.world.choose_title, "Choose the account whose TotK save to back up");
    CHECK_STR_EQ(f.result.message, "Cancelled. No backup was made");
}

void run_sd_backup_tests(void)
{
    RUN_TEST(a_backup_is_named_by_time_and_reason_and_holds_the_exact_zip);
    RUN_TEST(a_before_pull_backup_carries_its_reason_in_the_name);
    RUN_TEST(a_write_failure_reports_the_reason_and_leaves_no_part_file);
    RUN_TEST(a_backup_that_does_not_read_back_is_removed_and_reported);
    RUN_TEST(an_existing_backup_with_the_same_name_is_never_overwritten);
    RUN_TEST(pruning_keeps_the_newest_own_backups_and_never_touches_other_files);
    RUN_TEST(the_new_backup_survives_even_when_older_names_sort_after_it);
    RUN_TEST(own_names_follow_the_template_exactly);
    RUN_TEST(backup_to_sd_writes_the_save_zip_and_reports_its_path);
    RUN_TEST(backup_to_sd_refuses_while_totk_runs_and_mounts_nothing);
    RUN_TEST(backup_to_sd_reports_an_sd_failure_with_its_reason);
    RUN_TEST(backup_to_sd_uses_its_own_account_list_title);
}
