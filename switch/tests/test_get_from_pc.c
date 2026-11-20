#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "get_from_pc.h"
#include "test.h"
#include "test_fs.h"
#include "test_support.h"
#include "test_zip_read.h"

#define FINGERPRINT "sha256:e19ed4007a1fcdcc231a1c9bb8e9f73753913c0db5328afc635f94c12f6f4a5e"
#define ZIP_HEADERS                                                                                                      \
    "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 1\r\nContent-Type: application/zip\r\n"                                         \
    "X-TotkSync-Fingerprint: " FINGERPRINT "\r\nX-TotkSync-Modified: 2026-11-03T14:25:01+03:00\r\n\r\n"
#define BACKUP_NAME "totk-20261107-120000-before-pull.zip"
#define BACKUPS_REL "switch/totk-sync/backups"
#define NOT_CHANGED "Switch save not changed"
#define IDENTITY_SIZE 8192

typedef struct {
    fake_world_t world;
    fake_console_t console;
    faulty_files_t files;
    config_t cfg;
    scripted_reply_t script[2];
    char *ack_body;
    unsigned char *zip;
    size_t zip_size;
    char save[256];
    char sd[256];
    char save_before[IDENTITY_SIZE];
    char pc_save[IDENTITY_SIZE];
    pull_result_t result;
} pull_fixture_t;

static unsigned char *read_bytes(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *bytes = malloc((size_t)length);
    *size = bytes != NULL ? fread(bytes, 1, (size_t)length, file) : 0;
    fclose(file);
    return bytes;
}

static void use_pc_zip(pull_fixture_t *f, const char *contract_zip)
{
    char path[512];
    free(f->zip);
    snprintf(path, sizeof path, "%s/save-zip/%s", CONTRACTS_DIR, contract_zip);
    f->zip = read_bytes(path, &f->zip_size);
    CHECK(f->zip != NULL);
    f->world.binary_body = f->zip;
    f->world.binary_body_size = f->zip_size;
}

/* The Switch holds an older save; the PC answers GET /save with contracts/save-tree and records the ack. */
static void fixture_open(pull_fixture_t *f)
{
    memset(f, 0, sizeof *f);
    f->ack_body = test_read_file_in(CONTRACTS_DIR "/api", "ack-recorded.json");
    f->script[0] = (scripted_reply_t){ 200, 0, ZIP_HEADERS, NULL };
    f->script[1] = (scripted_reply_t){ 200, 0, LAUNCHER_MARKER_HEADERS, f->ack_body };
    fake_world_init(&f->world, f->script, 2);
    fake_console_init(&f->console, &f->world);
    faulty_files_init(&f->files);
    test_make_config(&f->cfg);
    CHECK(test_fs_make_root(f->save, sizeof f->save));
    CHECK(test_fs_write_file(f->save, "option.sav", "switch option", 13));
    CHECK(test_fs_write_file(f->save, "slot_00/progress.sav", "switch progress", 15));
    CHECK(test_fs_write_file(f->save, "slot_03/only-on-switch.sav", "switch only", 11));
    CHECK(test_fs_make_root(f->sd, sizeof f->sd));
    snprintf(f->console.mount_root, sizeof f->console.mount_root, "%s", f->save);
    f->console.is_space_known = true;
    f->console.data_bytes = 32LL * 1024 * 1024;
    f->console.journal_bytes = 8LL * 1024 * 1024;
    f->world.env.sd_root = f->sd;
    f->world.env.files = &f->files.ops;
    f->world.confirm_answer = true;
    use_pc_zip(f, "valid-save-tree.zip");
    test_tree_identity(f->save, f->save_before, sizeof f->save_before);
    char contract_save[256];
    CHECK(test_fs_make_contract_save(contract_save, sizeof contract_save));
    test_tree_identity(contract_save, f->pc_save, sizeof f->pc_save);
}

static void fixture_close(pull_fixture_t *f)
{
    CHECK(!f->console.is_mounted); /* unmounted on every path */
    CHECK(!f->console.is_home_blocked);
    CHECK(test_is_ascii_text(f->result.message));
    CHECK_STR_NOT_CONTAINS(f->result.message, CONTRACT_TOKEN);
    free(f->ack_body);
    free(f->zip);
    fake_world_release(&f->world);
}

static void run_pull(pull_fixture_t *f)
{
    get_from_pc(&f->world.env, &f->cfg, &f->result);
}

static void save_identity(const pull_fixture_t *f, char *out)
{
    test_tree_identity(f->save, out, IDENTITY_SIZE);
}

static bool sd_has(const pull_fixture_t *f, const char *name)
{
    char path[512];
    struct stat info;
    snprintf(path, sizeof path, "%s/%s/%s", f->sd, BACKUPS_REL, name);
    return stat(path, &info) == 0;
}

static int sd_backup_count(const pull_fixture_t *f)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", f->sd, BACKUPS_REL);
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

static void assert_untouched(pull_fixture_t *f)
{
    char now[IDENTITY_SIZE];
    save_identity(f, now);
    CHECK_STR_EQ(now, f->save_before);
    CHECK_INT_EQ(f->console.rw_mount_calls, 0);
    CHECK_INT_EQ(f->console.commit_calls, 0);
}

/* --- the main path ------------------------------------------------------------------------------------------- */

static void get_from_pc_restores_the_pc_save_exactly_and_confirms_the_sync(void)
{
    pull_fixture_t f;
    fixture_open(&f);

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_OK);
    CHECK_STR_EQ(f.result.message, "RESTORED FROM PC. Switch backup: " BACKUP_NAME);
    char now[IDENTITY_SIZE];
    save_identity(&f, now);
    CHECK_STR_EQ(now, f.pc_save);
    CHECK_INT_EQ(f.world.net_calls, 2);
    CHECK_STR_EQ(f.world.request_urls[0], "http://192.168.1.10:8765/save");
    CHECK_STR_EQ(f.world.request_urls[1], "http://192.168.1.10:8765/save/ack");
    CHECK_STR_EQ((const char *)f.world.sent_body.data, "{\"fingerprint\":\"" FINGERPRINT "\"}");
    CHECK_INT_EQ(f.console.home_block_calls, 1);
    CHECK_INT_EQ(f.console.home_unblock_calls, 1);
    CHECK(f.console.commit_calls >= 1);
    fixture_close(&f);
}

static void the_restore_question_shows_the_pc_date_file_count_and_size(void)
{
    pull_fixture_t f;
    fixture_open(&f);

    run_pull(&f);

    CHECK_INT_EQ(f.world.confirm_calls, 1);
    CHECK(strncmp(f.world.confirm_prompts[0], "PC save from 2026-11-03 14:25 (", 31) == 0);
    CHECK(strstr(f.world.confirm_prompts[0], " files, ") != NULL);
    CHECK(strstr(f.world.confirm_prompts[0], " KB). Restore to Switch? A - yes, B - no") != NULL);
    fixture_close(&f);
}

static void a_verified_before_pull_backup_of_the_old_save_is_made_before_writing(void)
{
    pull_fixture_t f;
    fixture_open(&f);

    run_pull(&f);

    CHECK(sd_has(&f, BACKUP_NAME));
    CHECK_STR_EQ(f.result.backup, BACKUP_NAME);
    char restored[256];
    CHECK(test_fs_make_root(restored, sizeof restored));
    body_t backup = { NULL, 0 };
    CHECK(sd_backup_read(f.sd, BACKUP_NAME, &backup));
    test_zip_t listing;
    CHECK(test_zip_open(backup.data, (size_t)backup.size, &listing));
    CHECK(test_zip_find(&listing, "slot_03/only-on-switch.sav") != NULL);
    CHECK_INT_EQ(f.console.mount_calls, 1);
    CHECK_INT_EQ(f.console.rw_mount_calls, 1);
    test_zip_close(&listing);
    body_free(&backup);
    fixture_close(&f);
}

static void b_at_the_question_changes_nothing_and_makes_no_backup(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.world.confirm_answer = false;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_CANCELLED);
    CHECK_STR_EQ(f.result.message, "Cancelled. " NOT_CHANGED);
    assert_untouched(&f);
    CHECK_INT_EQ(f.console.mount_calls, 0);
    CHECK_INT_EQ(sd_backup_count(&f), 0);
    CHECK_INT_EQ(f.world.net_calls, 1);
    fixture_close(&f);
}

/* --- refusals before any change -------------------------------------------------------------------------------- */

static void an_invalid_pc_zip_is_refused_before_the_question(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    use_pc_zip(&f, "invalid-no-option.zip");

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_INVALID_ZIP);
    CHECK_STR_EQ(f.result.message, "PC save is invalid: missing option.sav. " NOT_CHANGED);
    CHECK_INT_EQ(f.world.confirm_calls, 0);
    assert_untouched(&f);
    CHECK_INT_EQ(sd_backup_count(&f), 0);
    fixture_close(&f);
}

static void totk_started_after_a_changes_nothing(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.console.probe_results_count = 1;
    f.console.probe_results[0] = f.console.probe;
    f.console.probe.is_found = true;
    f.console.probe.pid = 5;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_GAME_RUNNING);
    CHECK_INT_EQ(f.console.probe_calls, 2);
    assert_untouched(&f);
    CHECK_INT_EQ(sd_backup_count(&f), 0);
    fixture_close(&f);
}

static void totk_started_during_the_backup_still_changes_nothing(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.console.probe_results_count = 2;
    f.console.probe_results[0] = f.console.probe;
    f.console.probe_results[1] = f.console.probe;
    f.console.probe.is_found = true;
    f.console.probe.pid = 5;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_GAME_RUNNING);
    CHECK_INT_EQ(f.console.probe_calls, 3);
    assert_untouched(&f);
    fixture_close(&f);
}

static void a_pc_save_bigger_than_the_switch_save_is_refused_with_both_sizes(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.console.data_bytes = 10;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_DOES_NOT_FIT);
    CHECK(strncmp(f.result.message, "PC save does not fit into the Switch save: ", 43) == 0);
    CHECK(strstr(f.result.message, "KB needed, 1 KB available. " NOT_CHANGED) != NULL);
    assert_untouched(&f);
    CHECK_INT_EQ(sd_backup_count(&f), 0);
    fixture_close(&f);
}

static void a_failed_backup_stops_before_the_save_is_opened_for_writing(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.files.only_under = f.sd;
    f.files.fail_step = FAULT_WRITE;
    f.files.fail_at = 1;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_BACKUP_FAILED);
    CHECK(strncmp(f.result.message, "SD backup failed: cannot write sd:/switch/totk-sync/backups/", 60) == 0);
    CHECK(strstr(f.result.message, "No space left on device. " NOT_CHANGED) != NULL);
    assert_untouched(&f);
    CHECK_INT_EQ(sd_backup_count(&f), 0);
    fixture_close(&f);
}

static void a_failed_writable_mount_leaves_the_save_alone(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.console.does_rw_mount_succeed = false;
    f.console.rw_mount_error = 0x1a2b;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_MOUNT_FAILED);
    CHECK_STR_EQ(f.result.message, "Cannot open the TotK save for writing (error 0x1a2b). " NOT_CHANGED);
    char now[IDENTITY_SIZE];
    save_identity(&f, now);
    CHECK_STR_EQ(now, f.save_before);
    CHECK_INT_EQ(f.world.net_calls, 1);
    fixture_close(&f);
}

static void download_refusals_say_why_and_touch_nothing(void)
{
    char *no_save = test_read_file_in(CONTRACTS_DIR "/api", "error-no-save.json");
    char *game = test_read_file_in(CONTRACTS_DIR "/api", "error-game-running.json");
    const struct {
        scripted_reply_t reply;
        const char *message;
    } cases[] = {
        { { 404, 0, LAUNCHER_MARKER_HEADERS, no_save }, "No save on PC yet: start TotK on PC once. " NOT_CHANGED },
        { { 423, 0, LAUNCHER_MARKER_HEADERS, game }, "TotK is running on PC: close it and retry. " NOT_CHANGED },
        { { 0, 7, NULL, NULL }, "PC not reachable: is the launcher open? check firewall and same Wi-Fi. " NOT_CHANGED },
        { { 200, 0, "HTTP/1.1 200 OK\r\n\r\n", "{}" }, "Not a TotK Sync launcher at 192.168.1.10:8765. " NOT_CHANGED },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        pull_fixture_t f;
        fixture_open(&f);
        f.script[0] = cases[i].reply;
        f.script[1] = cases[i].reply; /* a retried GET gets the same answer */
        f.world.binary_body = NULL;

        run_pull(&f);

        CHECK_INT_EQ(f.result.kind, PULL_DOWNLOAD_FAILED);
        CHECK_STR_EQ(f.result.message, cases[i].message);
        assert_untouched(&f);
        CHECK_INT_EQ(f.world.confirm_calls, 0);
        fixture_close(&f);
    }
    free(no_save);
    free(game);
}

static void the_pull_uses_its_own_account_list_title(void)
{
    pull_fixture_t f;
    static const uint64_t uid[2] = { 9, 9 };
    fixture_open(&f);
    account_make(&f.console.accounts[1], uid, "Second", 2);
    f.console.account_count = 2;
    f.world.choose_result = -1;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_CANCELLED);
    CHECK_STR_EQ(f.world.choose_title, "Choose the account to restore the PC save into");
    CHECK_INT_EQ(f.world.net_calls, 0);
    fixture_close(&f);
}

/* --- failures while writing -------------------------------------------------------------------------------------- */

static void a_write_failing_before_any_change_reports_the_save_unchanged(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    CHECK(test_fs_make_root(f.save, sizeof f.save));
    CHECK(test_fs_write_file(f.save, "option.sav", "x", 1));
    CHECK(test_fs_write_file(f.save, "slot_00/progress.sav", "y", 1));
    snprintf(f.console.mount_root, sizeof f.console.mount_root, "%s", f.save);
    f.files.only_under = f.save;
    f.files.fail_step = FAULT_REMOVE_FILE;
    f.files.fail_at = 1;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_WRITE_FAILED);
    CHECK(strncmp(f.result.message, "Write failed: cannot remove ", 28) == 0);
    CHECK(strstr(f.result.message, ". " NOT_CHANGED) != NULL);
    CHECK_INT_EQ(f.world.confirm_calls, 1);
    CHECK_INT_EQ(f.world.net_calls, 1);
    fixture_close(&f);
}

static void a_write_failing_midway_offers_the_backup_and_a_restores_it(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.files.only_under = f.save;
    f.files.fail_step = FAULT_WRITE;
    f.files.fail_at = 2;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_RESTORED_BACKUP);
    CHECK_STR_EQ(f.result.message, "Switch save restored from backup " BACKUP_NAME ". The PC save was not applied");
    CHECK_INT_EQ(f.world.confirm_calls, 2);
    CHECK(strncmp(f.world.confirm_prompts[1], "WRITE FAILED: cannot write ", 27) == 0);
    CHECK(strstr(f.world.confirm_prompts[1], "Switch save may be incomplete. A - restore backup " BACKUP_NAME ", B - leave as is")
          != NULL);
    char now[IDENTITY_SIZE];
    save_identity(&f, now);
    CHECK_STR_EQ(now, f.save_before);
    CHECK_INT_EQ(f.console.rw_mount_calls, 2);
    CHECK_INT_EQ(f.world.net_calls, 1);
    fixture_close(&f);
}

static void a_write_failing_midway_and_b_leaves_the_save_and_names_the_backup(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.files.only_under = f.save;
    f.files.fail_step = FAULT_WRITE;
    f.files.fail_at = 2;
    f.world.confirm_answer_count = 2;
    f.world.confirm_answers[0] = true;
    f.world.confirm_answers[1] = false;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_LEFT_INCOMPLETE);
    CHECK(strncmp(f.result.message, "WRITE FAILED: cannot write ", 27) == 0);
    CHECK(strstr(f.result.message, "Switch save left as is; backup: sd:/switch/totk-sync/backups/" BACKUP_NAME) != NULL);
    CHECK_INT_EQ(f.console.rw_mount_calls, 1);
    CHECK_INT_EQ(f.world.net_calls, 1);
    fixture_close(&f);
}

static void a_failing_last_commit_counts_as_a_failed_write_after_changes(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.console.fail_commit_at = 1;
    f.console.commit_error = 0x99;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_RESTORED_BACKUP);
    CHECK(strncmp(f.world.confirm_prompts[1], "WRITE FAILED: commit failed (error 0x99). ", 42) == 0);
    char now[IDENTITY_SIZE];
    save_identity(&f, now);
    CHECK_STR_EQ(now, f.save_before);
    fixture_close(&f);
}

static void when_the_restore_fails_too_the_backup_path_and_jksv_are_named(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.console.fail_commit_at = 1;
    f.console.fail_rw_mount_at = 2;
    f.console.rw_mount_error = 0x77;

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_RESTORE_FAILED);
    CHECK_STR_EQ(f.result.message, "Restore failed too: cannot open the TotK save for writing (error 0x77). "
                                   "Restore sd:/switch/totk-sync/backups/" BACKUP_NAME " with JKSV");
    CHECK(sd_has(&f, BACKUP_NAME));
    fixture_close(&f);
}

static void an_ack_that_does_not_arrive_is_a_warning_after_a_good_restore(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.script[1] = (scripted_reply_t){ 0, 7, NULL, NULL };

    run_pull(&f);

    CHECK_INT_EQ(f.result.kind, PULL_OK_ACK_FAILED);
    CHECK_STR_EQ(f.result.message, "RESTORED FROM PC. Switch backup: " BACKUP_NAME "\nRestored, but PC did not record the sync");
    char now[IDENTITY_SIZE];
    save_identity(&f, now);
    CHECK_STR_EQ(now, f.pc_save);
    fixture_close(&f);
}

static void home_is_locked_only_around_the_write(void)
{
    pull_fixture_t f;
    fixture_open(&f);
    f.world.confirm_answer = false;

    run_pull(&f);

    CHECK_INT_EQ(f.console.home_block_calls, 0);
    fixture_close(&f);
}

void run_get_from_pc_tests(void)
{
    RUN_TEST(get_from_pc_restores_the_pc_save_exactly_and_confirms_the_sync);
    RUN_TEST(the_restore_question_shows_the_pc_date_file_count_and_size);
    RUN_TEST(a_verified_before_pull_backup_of_the_old_save_is_made_before_writing);
    RUN_TEST(b_at_the_question_changes_nothing_and_makes_no_backup);
    RUN_TEST(an_invalid_pc_zip_is_refused_before_the_question);
    RUN_TEST(totk_started_after_a_changes_nothing);
    RUN_TEST(totk_started_during_the_backup_still_changes_nothing);
    RUN_TEST(a_pc_save_bigger_than_the_switch_save_is_refused_with_both_sizes);
    RUN_TEST(a_failed_backup_stops_before_the_save_is_opened_for_writing);
    RUN_TEST(a_failed_writable_mount_leaves_the_save_alone);
    RUN_TEST(download_refusals_say_why_and_touch_nothing);
    RUN_TEST(the_pull_uses_its_own_account_list_title);
    RUN_TEST(a_write_failing_before_any_change_reports_the_save_unchanged);
    RUN_TEST(a_write_failing_midway_offers_the_backup_and_a_restores_it);
    RUN_TEST(a_write_failing_midway_and_b_leaves_the_save_and_names_the_backup);
    RUN_TEST(a_failing_last_commit_counts_as_a_failed_write_after_changes);
    RUN_TEST(when_the_restore_fails_too_the_backup_path_and_jksv_are_named);
    RUN_TEST(an_ack_that_does_not_arrive_is_a_warning_after_a_good_restore);
    RUN_TEST(home_is_locked_only_around_the_write);
}
