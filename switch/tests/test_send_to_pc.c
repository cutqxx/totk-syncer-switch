#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <share.h>
#else
#include <sys/stat.h>
#endif

#include "log.h"
#include "send_to_pc.h"
#include "test.h"
#include "test_fs.h"
#include "test_support.h"
#include "test_zip_read.h"

#define BIG_LIMIT (64LL * 1024 * 1024)
#define LOG_REL "switch/totk-sync/log.txt"

#define CLOSE_TOTK "Close TotK first (HOME -> close the game)"
#define NOT_REACHABLE "PC not reachable: is the launcher open? check firewall and same Wi-Fi"

typedef struct {
    fake_world_t world;
    fake_console_t console;
    config_t cfg;
    send_result_t result;
    scripted_reply_t script[1];
    char *reply_body;
    log_sink_t log;
} send_fixture_t;

/* A save copied from contracts/save-tree, mounted by the fake console, and one scripted PC answer. */
static void fixture_open(send_fixture_t *f, long http_code, int curl_code, const char *headers, const char *contract_name)
{
    memset(f, 0, sizeof *f);
    f->reply_body = contract_name != NULL ? test_read_file_in(CONTRACTS_DIR "/api", contract_name) : NULL;
    f->script[0] = (scripted_reply_t){ http_code, curl_code, headers, f->reply_body };
    fake_world_init(&f->world, f->script, 1);
    fake_console_init(&f->console, &f->world);
    test_make_config(&f->cfg);
    CHECK(test_fs_make_contract_save(f->console.mount_root, sizeof f->console.mount_root));
}

static void fixture_close(send_fixture_t *f)
{
    fake_world_release(&f->world);
    free(f->reply_body);
}

static void fixture_open_success(send_fixture_t *f)
{
    fixture_open(f, 200, 0, LAUNCHER_MARKER_HEADERS, "put-replaced.json");
}

static void run_send(send_fixture_t *f)
{
    send_to_pc(&f->world.env, &f->cfg, BIG_LIMIT, &f->result);
}

static void check_nothing_mounted_and_nothing_sent(const send_fixture_t *f)
{
    CHECK_INT_EQ(f->console.mount_calls, 0);
    CHECK_INT_EQ(f->console.unmount_calls, 0);
    CHECK_INT_EQ(f->world.net_calls, 0);
    CHECK_INT_EQ(f->world.sleep_count, 0);
}

static void check_exactly_one_mount_and_one_unmount_before_any_request(const send_fixture_t *f)
{
    CHECK_INT_EQ(f->console.mount_calls, 1);
    CHECK_INT_EQ(f->console.unmount_calls, 1);
    CHECK_INT_EQ(f->console.net_calls_at_mount, 0);
    CHECK_INT_EQ(f->console.net_calls_at_unmount, 0);
}

static void make_two_accounts(send_fixture_t *f)
{
    static const uint64_t first[2] = { 1, 1 };
    static const uint64_t second[2] = { 2, 2 };
    account_make(&f->console.accounts[0], first, "Alice", 1);
    account_make(&f->console.accounts[1], second, "Bob", 2);
    f->console.account_count = 2;
}

static void make_custom_save(send_fixture_t *f, const char *rel, const void *content, size_t size)
{
    CHECK(test_fs_make_root(f->console.mount_root, sizeof f->console.mount_root));
    CHECK(test_fs_write_file(f->console.mount_root, rel, content, size));
}

static char *read_log_text(const char *root)
{
    char *text = test_read_file_in(root, LOG_REL);
    CHECK(text != NULL);
    return text != NULL ? text : calloc(1, 1);
}

static int count_occurrences(const char *text, const char *needle)
{
    int count = 0;
    for (const char *at = strstr(text, needle); at != NULL; at = strstr(at + 1, needle)) {
        count++;
    }
    return count;
}

/* ---- before anything is mounted ---- */

static void a_running_game_stops_the_send_before_mounting_or_sending(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    f.console.probe.is_found = true;
    f.console.probe.pid = 4242;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_GAME_RUNNING);
    CHECK_STR_EQ(f.result.message, CLOSE_TOTK);
    check_nothing_mounted_and_nothing_sent(&f);
    CHECK_INT_EQ(f.world.choose_calls, 0);
    CHECK_INT_EQ(f.world.ui_line_count, 1);
    CHECK_STR_EQ(f.world.ui_lines[0], "Checking that TotK is closed...");
    fixture_close(&f);
}

static void a_failed_game_check_is_handled_like_a_running_game_and_shows_the_error_code(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    f.console.probe.is_probe_ok = false;
    f.console.probe.error_code = 0x1A0F;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_GAME_CHECK_FAILED);
    CHECK_STR_EQ(f.result.message, "Cannot check whether TotK is running (error 0x1a0f). Nothing was read");
    check_nothing_mounted_and_nothing_sent(&f);
    fixture_close(&f);
}

static void a_probe_that_reports_nothing_is_a_failed_check_not_a_clear_game(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    f.console.is_probe_left_untouched = true;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_GAME_CHECK_FAILED);
    CHECK_STR_EQ(f.result.message, "Cannot check whether TotK is running (error 0x0). Nothing was read");
    check_nothing_mounted_and_nothing_sent(&f);
    fixture_close(&f);
}

static void a_save_on_no_account_says_so_and_mounts_nothing(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    f.console.account_count = 0;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_NO_SAVE_ON_CONSOLE);
    CHECK_STR_EQ(f.result.message, "No TotK save on this console");
    check_nothing_mounted_and_nothing_sent(&f);
    fixture_close(&f);
}

static void leaving_the_account_list_cancels_without_mounting_or_sending(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    make_two_accounts(&f);
    f.world.choose_result = -1;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_CANCELLED);
    CHECK_STR_EQ(f.result.message, "Cancelled. Nothing was sent");
    CHECK_INT_EQ(f.world.choose_calls, 1);
    check_nothing_mounted_and_nothing_sent(&f);
    fixture_close(&f);
}

static void a_single_account_is_used_without_asking(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    run_send(&f);
    CHECK_INT_EQ(f.world.choose_calls, 0);
    CHECK_INT_EQ(f.console.mount_calls, 1);
    CHECK_STR_EQ(f.console.mounted_account.name, "Player");
    CHECK_INT_EQ(f.console.mounted_account.uid[0], 0x1111);
    fixture_close(&f);
}

static void with_several_accounts_the_chosen_one_is_mounted(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    make_two_accounts(&f);
    f.world.choose_result = 1;
    run_send(&f);
    CHECK_INT_EQ(f.world.choose_calls, 1);
    CHECK_INT_EQ(f.world.choose_offered, 2);
    CHECK_INT_EQ(f.console.mount_calls, 1);
    CHECK_STR_EQ(f.console.mounted_account.name, "Bob");
    CHECK_INT_EQ(f.console.mounted_account.uid[0], 2);
    CHECK_STR_EQ(f.world.ui_lines[1], "Reading save of Bob...");
    CHECK_INT_EQ(f.result.kind, SEND_OK);
    fixture_close(&f);
}

static void a_save_that_cannot_be_mounted_reports_the_error_and_sends_nothing(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    f.console.does_mount_succeed = false;
    f.console.mount_error = 0x2002;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_MOUNT_FAILED);
    CHECK_STR_EQ(f.result.message, "Cannot open the TotK save read-only (error 0x2002). Nothing was sent");
    CHECK_INT_EQ(f.console.mount_calls, 1);
    CHECK_INT_EQ(f.console.unmount_calls, 0);
    CHECK_INT_EQ(f.world.net_calls, 0);
    fixture_close(&f);
}

/* ---- the save is read, then unmounted before the network is touched ---- */

static void a_successful_send_reports_the_backup_name_of_the_pc(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_OK);
    CHECK_STR_EQ(f.result.message, "SENT TO PC. PC backup: pc-20261103-142501-before-switch.zip");
    CHECK_STR_EQ(f.result.backup, "pc-20261103-142501-before-switch.zip");
    fixture_close(&f);
}

static void a_successful_send_mounts_once_and_unmounts_once_before_the_request(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    run_send(&f);
    check_exactly_one_mount_and_one_unmount_before_any_request(&f);
    CHECK_INT_EQ(f.world.net_calls, 1);
    fixture_close(&f);
}

static void the_screen_shows_the_steps_in_order_with_sizes(void)
{
    send_fixture_t f;
    char expected[TS_LINE_SIZE];
    fixture_open_success(&f);
    run_send(&f);
    CHECK_INT_EQ(f.world.ui_line_count, 6);
    CHECK_STR_EQ(f.world.ui_lines[0], "Checking that TotK is closed...");
    CHECK_STR_EQ(f.world.ui_lines[1], "Reading save of Player...");
    CHECK(strncmp(f.world.ui_lines[2], "Save read: 31 files, 10 folders, ", 33) == 0);
    snprintf(expected, sizeof expected, "ZIP ready: %lld KB", (long long)(f.world.sent_body.size / 1024));
    CHECK_STR_EQ(f.world.ui_lines[3], expected);
    snprintf(expected, sizeof expected, "Sending to PC... (%lld KB)", (long long)(f.world.sent_body.size / 1024));
    CHECK_STR_EQ(f.world.ui_lines[4], expected);
    CHECK_STR_EQ(f.world.ui_lines[5], "Please wait: buttons and HOME do not respond until this ends");
    fixture_close(&f);
}

static void the_request_is_a_put_to_save_with_the_token_and_expect_100_continue(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    run_send(&f);
    CHECK_INT_EQ(f.world.last_request.method, HTTP_PUT);
    CHECK_STR_EQ(f.world.last_request.url, "http://192.168.1.10:8765/save");
    CHECK_STR_EQ(f.world.last_request.headers[0], "Authorization: Bearer " CONTRACT_TOKEN);
    CHECK_STR_EQ(f.world.last_request.headers[2], "Expect: 100-continue");
    CHECK_INT_EQ(f.world.last_request.expect_100_timeout_ms, 3000);
    fixture_close(&f);
}

static void the_body_sent_is_the_contract_zip_of_the_mounted_save(void)
{
    send_fixture_t f;
    test_zip_t listing;
    fixture_open_success(&f);
    run_send(&f);
    cJSON *cases = test_load_cases();
    const cJSON *expected = cases != NULL ? test_find_case(cases, "valid-save-tree.zip") : NULL;
    CHECK(expected != NULL);
    CHECK(f.world.sent_body.data != NULL);
    if (expected != NULL && f.world.sent_body.data != NULL && test_zip_open(f.world.sent_body.data, (size_t)f.world.sent_body.size, &listing)) {
        const cJSON *dirs = cJSON_GetObjectItemCaseSensitive(expected, "saved_dirs");
        const cJSON *files = cJSON_GetObjectItemCaseSensitive(expected, "saved_files");
        CHECK_INT_EQ(listing.count, cJSON_GetArraySize(dirs) + cJSON_GetArraySize(files));
        const cJSON *name = NULL;
        cJSON_ArrayForEach(name, files)
        {
            CHECK(test_zip_find(&listing, name->valuestring) != NULL);
        }
        test_zip_close(&listing);
    }
    cJSON_Delete(cases);
    fixture_close(&f);
}

static void the_whole_send_never_changes_the_mounted_save(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    char *before = test_fs_snapshot(f.console.mount_root);
    run_send(&f);
    char *after = test_fs_snapshot(f.console.mount_root);
    CHECK_INT_EQ(f.result.kind, SEND_OK);
    CHECK(before != NULL && after != NULL);
    if (before != NULL && after != NULL) {
        CHECK_STR_EQ(after, before);
    }
    free(before);
    free(after);
    fixture_close(&f);
}

/* ---- every outcome after a mount unmounts exactly once ---- */

static void a_save_above_the_limit_shows_both_sizes_and_sends_nothing(void)
{
    send_fixture_t f;
    static const char kilobytes3[3072] = { 'x' };
    fixture_open_success(&f);
    make_custom_save(&f, "big.sav", kilobytes3, sizeof kilobytes3);
    send_to_pc(&f.world.env, &f.cfg, 2048, &f.result);
    CHECK_INT_EQ(f.result.kind, SEND_PACK_FAILED);
    CHECK_STR_EQ(f.result.message, "Save is too large: 3 KB (limit 2 KB). Nothing was sent");
    CHECK_INT_EQ(f.console.mount_calls, 1);
    CHECK_INT_EQ(f.console.unmount_calls, 1);
    CHECK_INT_EQ(f.world.net_calls, 0);
    fixture_close(&f);
}

static void an_unreadable_file_unmounts_names_the_file_and_sends_nothing(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
#ifdef _WIN32
    char path[512];
    snprintf(path, sizeof path, "%s/option.sav", f.console.mount_root);
    FILE *held = _fsopen(path, "rb", _SH_DENYRW);
    CHECK(held != NULL);
#else
    char path[512];
    snprintf(path, sizeof path, "%s/option.sav", f.console.mount_root);
    CHECK(chmod(path, 0) == 0);
#endif
    run_send(&f);
#ifdef _WIN32
    if (held != NULL) {
        fclose(held);
    }
#else
    chmod(path, 0644);
#endif
    CHECK_INT_EQ(f.result.kind, SEND_PACK_FAILED);
    CHECK_STR_EQ(f.result.message, "Cannot read the save at option.sav. Nothing was sent");
    CHECK_INT_EQ(f.console.mount_calls, 1);
    CHECK_INT_EQ(f.console.unmount_calls, 1);
    CHECK_INT_EQ(f.world.net_calls, 0);
    fixture_close(&f);
}

static void a_name_the_pc_would_reject_unmounts_and_sends_nothing(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    CHECK(test_fs_write_file(f.console.mount_root, "caf\xE9.sav", "x", 1));
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_PACK_FAILED);
    CHECK(strncmp(f.result.message, "Save has a name that cannot be sent safely: caf", 47) == 0);
    CHECK(strstr(f.result.message, ". Nothing was sent") != NULL);
    CHECK_INT_EQ(f.console.unmount_calls, 1);
    CHECK_INT_EQ(f.world.net_calls, 0);
    fixture_close(&f);
}

static void a_name_the_pc_would_reject_is_shown_as_plain_ascii(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    CHECK(test_fs_write_file(f.console.mount_root, "caf\xE9.sav", "x", 1));
    run_send(&f);
    CHECK(test_is_ascii_text(f.result.message));
    fixture_close(&f);
}

static void a_connection_that_never_opens_is_retried_once_after_3_seconds_then_reported(void)
{
    send_fixture_t f;
    fixture_open(&f, 0, 7, NULL, NULL);
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_NETWORK_FAILED);
    CHECK_STR_EQ(f.result.message, NOT_REACHABLE);
    CHECK_INT_EQ(f.world.net_calls, 2);
    CHECK_INT_EQ(f.world.sleep_count, 1);
    CHECK_INT_EQ(f.world.slept_seconds[0], 3);
    CHECK_STR_EQ(f.world.ui_lines[6], "Retrying in 3 s...");
    check_exactly_one_mount_and_one_unmount_before_any_request(&f);
    fixture_close(&f);
}

static void both_attempts_send_the_same_zip(void)
{
    send_fixture_t f;
    fixture_open(&f, 0, 6, NULL, NULL);
    run_send(&f);
    CHECK_INT_EQ(f.world.net_calls, 2);
    CHECK(f.world.sent_body.size > 0);
    CHECK(!f.world.is_sent_body_changed);
    fixture_close(&f);
}

static void a_dropped_connection_after_the_body_started_is_not_retried_and_warns(void)
{
    send_fixture_t f;
    fixture_open(&f, 0, 56, NULL, NULL);
    f.world.reply_bytes_sent = 4096;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_NETWORK_FAILED);
    CHECK_STR_EQ(f.result.message,
                 "Connection lost while sending (curl 56). The PC may have received the save: check the launcher before retrying");
    CHECK_INT_EQ(f.world.net_calls, 1);
    CHECK_INT_EQ(f.world.sleep_count, 0);
    check_exactly_one_mount_and_one_unmount_before_any_request(&f);
    fixture_close(&f);
}

static void a_timeout_is_not_retried_either(void)
{
    send_fixture_t f;
    fixture_open(&f, 0, 28, NULL, NULL);
    f.world.reply_bytes_sent = 4096;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_NETWORK_FAILED);
    CHECK_STR_EQ(f.result.message,
                 "Connection lost while sending (curl 28). The PC may have received the save: check the launcher before retrying");
    CHECK_INT_EQ(f.world.net_calls, 1);
    CHECK_INT_EQ(f.world.sleep_count, 0);
    fixture_close(&f);
}

static void a_refusal_of_the_pc_unmounts_once_and_is_not_retried(void)
{
    static const struct {
        long code;
        const char *contract;
        const char *message;
    } refusals[] = {
        { 423, "error-game-running.json", "TotK is running on PC: close it and retry" },
        { 503, "error-busy.json", "PC is busy: retry in a moment" },
        { 404, "error-no-save.json", "No save on PC yet: start TotK on PC once" },
        { 400, "error-invalid-zip.json", "PC rejected the save: missing option.sav" },
        { 401, "error-unauthorized.json", "Wrong token: save switch-config.json from the PC launcher" },
        { 500, "error-write-failed.json", "PC error: backup failed, PC save not changed: <system message>" },
        { 413, "error-too-large.json", "PC error: HTTP 413" },
    };
    for (size_t i = 0; i < sizeof refusals / sizeof refusals[0]; i++) {
        send_fixture_t f;
        fixture_open(&f, refusals[i].code, 0, LAUNCHER_MARKER_HEADERS, refusals[i].contract);
        run_send(&f);
        CHECK_INT_EQ(f.result.kind, SEND_PC_REFUSED);
        CHECK_STR_EQ(f.result.message, refusals[i].message);
        CHECK_INT_EQ(f.world.net_calls, 1);
        CHECK_INT_EQ(f.world.sleep_count, 0);
        check_exactly_one_mount_and_one_unmount_before_any_request(&f);
        CHECK(f.result.backup[0] == '\0');
        fixture_close(&f);
    }
}

static void a_refusal_followed_by_a_broken_connection_is_still_shown_as_the_refusal(void)
{
    send_fixture_t f;
    fixture_open(&f, 423, 56, LAUNCHER_MARKER_HEADERS, "error-game-running.json");
    f.world.reply_bytes_sent = 300;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_PC_REFUSED);
    CHECK_STR_EQ(f.result.message, "TotK is running on PC: close it and retry");
    CHECK_INT_EQ(f.world.net_calls, 1);
    check_exactly_one_mount_and_one_unmount_before_any_request(&f);
    fixture_close(&f);
}

static void an_answer_from_a_foreign_server_is_reported_as_not_a_launcher(void)
{
    send_fixture_t f;
    fixture_open(&f, 200, 0, "HTTP/1.1 200 OK\r\n\r\n", "put-replaced.json");
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_PC_REFUSED);
    CHECK_STR_EQ(f.result.message, "Not a TotK Sync launcher at 192.168.1.10:8765");
    fixture_close(&f);
}

static void a_pc_text_that_echoes_the_token_never_reaches_the_result(void)
{
    send_fixture_t f;
    fixture_open(&f, 500, 0, LAUNCHER_MARKER_HEADERS, NULL);
    f.script[0].body = "{\"error\":\"write_failed\",\"reason\":\"token " CONTRACT_TOKEN " rejected\"}";
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_PC_REFUSED);
    CHECK_STR_NOT_CONTAINS(f.result.message, CONTRACT_TOKEN);
    CHECK(strstr(f.result.message, "PC error: token ") != NULL);
    fixture_close(&f);
}

static void every_failure_message_is_plain_ascii_and_free_of_the_token(void)
{
    static const struct {
        long code;
        int curl;
    } outcomes[] = { { 0, 7 }, { 0, 28 }, { 0, 35 }, { 0, 56 }, { 401, 0 }, { 403, 0 }, { 500, 0 }, { 302, 0 } };
    for (size_t i = 0; i < sizeof outcomes / sizeof outcomes[0]; i++) {
        send_fixture_t f;
        fixture_open(&f, outcomes[i].code, outcomes[i].curl, LAUNCHER_MARKER_HEADERS, "error-write-failed.json");
        f.world.reply_bytes_sent = 10;
        run_send(&f);
        CHECK(f.result.kind != SEND_OK);
        CHECK(test_is_ascii_text(f.result.message));
        CHECK_STR_NOT_CONTAINS(f.result.message, CONTRACT_TOKEN);
        CHECK(f.result.message[0] != '\0');
        fixture_close(&f);
    }
}

/* ---- log.txt ---- */

static void each_send_writes_one_transfer_line_with_sizes_time_speed_and_codes(void)
{
    send_fixture_t f;
    char log_root[256];
    char expected[TS_LINE_SIZE];
    test_zip_t listing;
    fixture_open_success(&f);
    CHECK(test_fs_make_root(log_root, sizeof log_root));
    log_sink_init(&f.log, log_root, CONTRACT_TOKEN);
    f.world.env.log = &f.log;
    f.world.reply_bytes_sent = 2048;
    f.world.reply_seconds = 2.0;
    run_send(&f);
    char *log = read_log_text(log_root);
    CHECK_INT_EQ(count_occurrences(log, "PUT /save: zip "), 1);
    CHECK(f.world.sent_body.data != NULL && test_zip_open(f.world.sent_body.data, (size_t)f.world.sent_body.size, &listing));
    long long unpacked = 0;
    for (int i = 0; i < listing.count; i++) {
        unpacked += (long long)listing.items[i].size;
    }
    snprintf(expected, sizeof expected,
             "[2026-11-07 12:00:00] PUT /save: zip %lld B, unpacked %lld B (31 files, 10 folders), compression %.1f:1, "
             "2048 B sent in 2.0 s (1 KB/s), HTTP 200, curl 0\n",
             (long long)f.world.sent_body.size, unpacked, (double)unpacked / (double)f.world.sent_body.size);
    CHECK(strstr(log, expected) != NULL);
    test_zip_close(&listing);
    free(log);
    fixture_close(&f);
}

static void a_retried_send_still_writes_a_single_transfer_line_but_both_attempts(void)
{
    send_fixture_t f;
    char log_root[256];
    fixture_open(&f, 0, 7, NULL, NULL);
    CHECK(test_fs_make_root(log_root, sizeof log_root));
    log_sink_init(&f.log, log_root, CONTRACT_TOKEN);
    f.world.env.log = &f.log;
    run_send(&f);
    char *log = read_log_text(log_root);
    CHECK_INT_EQ(count_occurrences(log, "PUT /save: zip "), 1);
    CHECK_INT_EQ(count_occurrences(log, "PUT attempt "), 2);
    CHECK(strstr(log, "HTTP 0, curl 7") != NULL);
    free(log);
    fixture_close(&f);
}

static void the_log_has_neither_the_token_nor_any_body(void)
{
    send_fixture_t f;
    char log_root[256];
    fixture_open(&f, 500, 0, LAUNCHER_MARKER_HEADERS, NULL);
    f.script[0].body = "{\"error\":\"write_failed\",\"reason\":\"secret-reason-text\"}";
    CHECK(test_fs_make_root(log_root, sizeof log_root));
    log_sink_init(&f.log, log_root, CONTRACT_TOKEN);
    f.world.env.log = &f.log;
    run_send(&f);
    char *log = read_log_text(log_root);
    CHECK_STR_NOT_CONTAINS(log, CONTRACT_TOKEN);
    CHECK_STR_NOT_CONTAINS(log, "secret-reason-text");
    CHECK_STR_NOT_CONTAINS(log, "PK");
    CHECK_STR_NOT_CONTAINS(log, "slot_00/progress.sav");
    free(log);
    fixture_close(&f);
}

static void a_send_that_stops_before_the_network_writes_no_transfer_line(void)
{
    send_fixture_t f;
    char log_root[256];
    fixture_open_success(&f);
    f.console.probe.is_found = true;
    f.console.probe.pid = 5;
    CHECK(test_fs_make_root(log_root, sizeof log_root));
    log_sink_init(&f.log, log_root, CONTRACT_TOKEN);
    f.world.env.log = &f.log;
    run_send(&f);
    char *text = test_read_file_in(log_root, LOG_REL);
    CHECK(text == NULL || count_occurrences(text, "PUT /save") == 0);
    free(text);
    fixture_close(&f);
}

static void a_log_that_cannot_be_written_is_reported_once_and_the_send_still_succeeds(void)
{
    send_fixture_t f;
    fixture_open_success(&f);
    log_sink_init(&f.log, "build/tmp/no-such-root/nested", CONTRACT_TOKEN);
    f.world.env.log = &f.log;
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_OK);
    CHECK_INT_EQ(f.world.ui_line_count, 7);
    CHECK_STR_EQ(f.world.ui_lines[6], "Log unavailable");
    fixture_close(&f);
}

/* ---- texts and the transfer line on their own ---- */

static void pack_failures_have_the_exact_texts_from_the_spec(void)
{
    char text[TS_LINE_SIZE];
    pack_stats_t stats;
    memset(&stats, 0, sizeof stats);
    stats.total_bytes = 70LL * 1024 * 1024 + 1023;
    send_describe_pack_failure(PACK_ERR_TOO_LARGE, &stats, TS_MAX_SAVE_BYTES, text, sizeof text);
    CHECK_STR_EQ(text, "Save is too large: 71680 KB (limit 65536 KB). Nothing was sent");
    send_describe_pack_failure(PACK_ERR_TOO_MANY_ENTRIES, &stats, TS_MAX_SAVE_BYTES, text, sizeof text);
    CHECK_STR_EQ(text, "Save has more than 1000 files and folders. Nothing was sent");
    snprintf(stats.problem_path, sizeof stats.problem_path, "slot_00/bad name?");
    send_describe_pack_failure(PACK_ERR_BAD_NAME, &stats, TS_MAX_SAVE_BYTES, text, sizeof text);
    CHECK_STR_EQ(text, "Save has a name that cannot be sent safely: slot_00/bad name?. Nothing was sent");
    send_describe_pack_failure(PACK_ERR_READ, &stats, TS_MAX_SAVE_BYTES, text, sizeof text);
    CHECK_STR_EQ(text, "Cannot read the save at slot_00/bad name?. Nothing was sent");
    send_describe_pack_failure(PACK_ERR_ZIP, &stats, TS_MAX_SAVE_BYTES, text, sizeof text);
    CHECK_STR_EQ(text, "Cannot build the ZIP (out of memory?). Nothing was sent");
}

static void the_transfer_line_has_every_field_the_spec_lists(void)
{
    char note[TS_LINE_SIZE];
    pack_stats_t stats;
    http_response_t resp;
    memset(&stats, 0, sizeof stats);
    http_response_init(&resp);
    stats.zip_bytes = 2048;
    stats.total_bytes = 8192;
    stats.files = 31;
    stats.directories = 10;
    resp.bytes_sent = 2048;
    resp.seconds = 2.0;
    resp.http_code = 200;
    resp.curl_code = 0;
    send_format_transfer_note(&stats, &resp, note, sizeof note);
    CHECK_STR_EQ(note,
                 "PUT /save: zip 2048 B, unpacked 8192 B (31 files, 10 folders), compression 4.0:1, "
                 "2048 B sent in 2.0 s (1 KB/s), HTTP 200, curl 0");
}

static void the_transfer_line_survives_a_zero_time_and_an_empty_zip(void)
{
    char note[TS_LINE_SIZE];
    pack_stats_t stats;
    http_response_t resp;
    memset(&stats, 0, sizeof stats);
    http_response_init(&resp);
    resp.curl_code = 7;
    send_format_transfer_note(&stats, &resp, note, sizeof note);
    CHECK_STR_EQ(note,
                 "PUT /save: zip 0 B, unpacked 0 B (0 files, 0 folders), compression 0.0:1, "
                 "0 B sent in 0.0 s (0 KB/s), HTTP 0, curl 7");
}

static void the_transfer_line_into_a_small_buffer_stays_terminated(void)
{
    char note[16];
    pack_stats_t stats;
    http_response_t resp;
    memset(&stats, 0, sizeof stats);
    http_response_init(&resp);
    memset(note, 0x5A, sizeof note);
    send_format_transfer_note(&stats, &resp, note, 8);
    CHECK(memchr(note, '\0', 8) != NULL);
    CHECK_INT_EQ((unsigned char)note[8], 0x5A);
}

void run_send_to_pc_tests(void)
{
    RUN_TEST(a_running_game_stops_the_send_before_mounting_or_sending);
    RUN_TEST(a_failed_game_check_is_handled_like_a_running_game_and_shows_the_error_code);
    RUN_TEST(a_probe_that_reports_nothing_is_a_failed_check_not_a_clear_game);
    RUN_TEST(a_save_on_no_account_says_so_and_mounts_nothing);
    RUN_TEST(leaving_the_account_list_cancels_without_mounting_or_sending);
    RUN_TEST(a_single_account_is_used_without_asking);
    RUN_TEST(with_several_accounts_the_chosen_one_is_mounted);
    RUN_TEST(a_save_that_cannot_be_mounted_reports_the_error_and_sends_nothing);
    RUN_TEST(a_successful_send_reports_the_backup_name_of_the_pc);
    RUN_TEST(a_successful_send_mounts_once_and_unmounts_once_before_the_request);
    RUN_TEST(the_screen_shows_the_steps_in_order_with_sizes);
    RUN_TEST(the_request_is_a_put_to_save_with_the_token_and_expect_100_continue);
    RUN_TEST(the_body_sent_is_the_contract_zip_of_the_mounted_save);
    RUN_TEST(the_whole_send_never_changes_the_mounted_save);
    RUN_TEST(a_save_above_the_limit_shows_both_sizes_and_sends_nothing);
    RUN_TEST(an_unreadable_file_unmounts_names_the_file_and_sends_nothing);
    RUN_TEST(a_name_the_pc_would_reject_unmounts_and_sends_nothing);
    RUN_TEST(a_name_the_pc_would_reject_is_shown_as_plain_ascii);
    RUN_TEST(a_connection_that_never_opens_is_retried_once_after_3_seconds_then_reported);
    RUN_TEST(both_attempts_send_the_same_zip);
    RUN_TEST(a_dropped_connection_after_the_body_started_is_not_retried_and_warns);
    RUN_TEST(a_timeout_is_not_retried_either);
    RUN_TEST(a_refusal_of_the_pc_unmounts_once_and_is_not_retried);
    RUN_TEST(a_refusal_followed_by_a_broken_connection_is_still_shown_as_the_refusal);
    RUN_TEST(an_answer_from_a_foreign_server_is_reported_as_not_a_launcher);
    RUN_TEST(a_pc_text_that_echoes_the_token_never_reaches_the_result);
    RUN_TEST(every_failure_message_is_plain_ascii_and_free_of_the_token);
    RUN_TEST(each_send_writes_one_transfer_line_with_sizes_time_speed_and_codes);
    RUN_TEST(a_retried_send_still_writes_a_single_transfer_line_but_both_attempts);
    RUN_TEST(the_log_has_neither_the_token_nor_any_body);
    RUN_TEST(a_send_that_stops_before_the_network_writes_no_transfer_line);
    RUN_TEST(a_log_that_cannot_be_written_is_reported_once_and_the_send_still_succeeds);
    RUN_TEST(pack_failures_have_the_exact_texts_from_the_spec);
    RUN_TEST(the_transfer_line_has_every_field_the_spec_lists);
    RUN_TEST(the_transfer_line_survives_a_zero_time_and_an_empty_zip);
    RUN_TEST(the_transfer_line_into_a_small_buffer_stays_terminated);
}
