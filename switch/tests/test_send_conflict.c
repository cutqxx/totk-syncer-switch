#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "send_to_pc.h"
#include "test.h"
#include "test_fs.h"
#include "test_support.h"

#define BIG_LIMIT (64LL * 1024 * 1024)
#define SAVE_URL "http://192.168.1.10:8765/save"
#define FORCED_URL "http://192.168.1.10:8765/save?force=1"
#define OVERWRITE_PROMPT                                                                                       \
    "PC save changed since last sync (PC: 2026-11-03 14:25, last sync: 2026-11-03 12:00). Overwrite PC save? " \
    "A - yes, B - no"
#define NEVER_SYNCED_PROMPT                                                                                \
    "PC save changed since last sync (PC: 2026-11-03 14:25, last sync: never). Overwrite PC save? A - yes, " \
    "B - no"

typedef struct {
    fake_world_t world;
    fake_console_t console;
    config_t cfg;
    send_result_t result;
    scripted_reply_t script[2];
    char *bodies[2];
} conflict_fixture_t;

/* A mounted contract save and up to two scripted PC answers: the first to PUT /save, the second to whatever comes next. */
static void fixture_open(conflict_fixture_t *f, long first_code, const char *first_contract, long second_code, const char *second_contract)
{
    memset(f, 0, sizeof *f);
    f->bodies[0] = first_contract != NULL ? test_read_file_in(CONTRACTS_DIR "/api", first_contract) : NULL;
    f->bodies[1] = second_contract != NULL ? test_read_file_in(CONTRACTS_DIR "/api", second_contract) : NULL;
    f->script[0] = (scripted_reply_t){ first_code, 0, LAUNCHER_MARKER_HEADERS, f->bodies[0] };
    f->script[1] = (scripted_reply_t){ second_code, 0, LAUNCHER_MARKER_HEADERS, f->bodies[1] };
    fake_world_init(&f->world, f->script, second_code != 0 ? 2 : 1);
    fake_console_init(&f->console, &f->world);
    test_make_config(&f->cfg);
    CHECK(test_fs_make_contract_save(f->console.mount_root, sizeof f->console.mount_root));
}

static void fixture_close(conflict_fixture_t *f)
{
    fake_world_release(&f->world);
    free(f->bodies[0]);
    free(f->bodies[1]);
}

static void run_send(conflict_fixture_t *f)
{
    send_to_pc(&f->world.env, &f->cfg, BIG_LIMIT, &f->result);
}

static void the_user_answers(conflict_fixture_t *f, bool is_yes)
{
    f->world.confirm_answer = is_yes;
}

/* ---- the question ---- */

static void a_conflict_asks_the_overwrite_question_with_both_dates(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 0, NULL);
    the_user_answers(&f, false);
    run_send(&f);
    CHECK_INT_EQ(f.world.confirm_calls, 1);
    CHECK_STR_EQ(f.world.confirm_prompt, OVERWRITE_PROMPT);
    fixture_close(&f);
}

static void a_conflict_with_no_last_sync_says_never_in_the_question(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict-never-synced.json", 0, NULL);
    the_user_answers(&f, false);
    run_send(&f);
    CHECK_INT_EQ(f.world.confirm_calls, 1);
    CHECK_STR_EQ(f.world.confirm_prompt, NEVER_SYNCED_PROMPT);
    fixture_close(&f);
}

static void the_first_request_of_a_send_is_never_forced(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 0, NULL);
    the_user_answers(&f, false);
    run_send(&f);
    CHECK_STR_EQ(f.world.request_urls[0], SAVE_URL);
    fixture_close(&f);
}

/* ---- B: nothing is sent ---- */

static void answering_b_sends_nothing_more_and_reports_the_cancellation(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 200, "put-replaced.json");
    the_user_answers(&f, false);
    run_send(&f);
    CHECK_INT_EQ(f.world.net_calls, 1);
    CHECK_INT_EQ(f.result.kind, SEND_CANCELLED);
    CHECK_STR_EQ(f.result.message, "Cancelled. Nothing was sent");
    CHECK(f.result.backup[0] == '\0');
    fixture_close(&f);
}

static void answering_b_does_not_read_the_save_again(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 200, "put-replaced.json");
    the_user_answers(&f, false);
    run_send(&f);
    CHECK_INT_EQ(f.console.mount_calls, 1);
    CHECK_INT_EQ(f.console.unmount_calls, 1);
    fixture_close(&f);
}

/* ---- A: the same ZIP again, with force ---- */

static void answering_a_sends_a_second_put_to_save_with_force_1(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 200, "put-replaced.json");
    the_user_answers(&f, true);
    run_send(&f);
    CHECK_INT_EQ(f.world.net_calls, 2);
    CHECK_STR_EQ(f.world.request_urls[0], SAVE_URL);
    CHECK_STR_EQ(f.world.request_urls[1], FORCED_URL);
    CHECK_INT_EQ(f.world.last_request.method, HTTP_PUT);
    CHECK_STR_EQ(f.world.last_request.headers[0], "Authorization: Bearer " CONTRACT_TOKEN);
    fixture_close(&f);
}

static void the_forced_request_carries_exactly_the_same_zip_as_the_first(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 200, "put-replaced.json");
    the_user_answers(&f, true);
    run_send(&f);
    CHECK(f.world.sent_body.data != NULL);
    CHECK(f.world.sent_body.size > 0);
    CHECK(!f.world.is_sent_body_changed);
    fixture_close(&f);
}

static void answering_a_reads_the_save_only_once_and_never_before_the_network_call_of_the_first_put(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 200, "put-replaced.json");
    the_user_answers(&f, true);
    run_send(&f);
    CHECK_INT_EQ(f.console.mount_calls, 1);
    CHECK_INT_EQ(f.console.unmount_calls, 1);
    CHECK_INT_EQ(f.console.net_calls_at_mount, 0);
    CHECK_INT_EQ(f.console.net_calls_at_unmount, 0);
    fixture_close(&f);
}

static void answering_a_and_getting_replaced_reports_the_pc_backup(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 200, "put-replaced.json");
    the_user_answers(&f, true);
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_OK);
    CHECK_STR_EQ(f.result.message, "SENT TO PC. PC backup: pc-20261103-142501-before-switch.zip");
    CHECK_STR_EQ(f.result.backup, "pc-20261103-142501-before-switch.zip");
    fixture_close(&f);
}

static void the_question_is_asked_only_once_even_when_the_forced_put_is_refused_with_a_conflict_again(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 409, "error-conflict.json");
    the_user_answers(&f, true);
    run_send(&f);
    CHECK_INT_EQ(f.world.confirm_calls, 1);
    CHECK_INT_EQ(f.world.net_calls, 2);
    CHECK_INT_EQ(f.result.kind, SEND_PC_REFUSED);
    CHECK_STR_EQ(f.result.message, "PC error: HTTP 409");
    fixture_close(&f);
}

static void a_refusal_of_the_forced_put_is_shown_like_any_refusal(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 423, "error-game-running.json");
    the_user_answers(&f, true);
    run_send(&f);
    CHECK_INT_EQ(f.world.confirm_calls, 1);
    CHECK_INT_EQ(f.result.kind, SEND_PC_REFUSED);
    CHECK_STR_EQ(f.result.message, "TotK is running on PC: close it and retry");
    fixture_close(&f);
}

static void the_forced_put_answered_with_unchanged_says_the_pc_already_has_the_save(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 200, "put-unchanged.json");
    the_user_answers(&f, true);
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_UNCHANGED);
    CHECK_STR_EQ(f.result.message, "PC already has this save");
    fixture_close(&f);
}

static void the_screen_shows_the_sending_step_again_for_the_forced_put(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 200, "put-replaced.json");
    the_user_answers(&f, true);
    run_send(&f);
    CHECK_INT_EQ(f.world.ui_line_count, 8);
    CHECK(strncmp(f.world.ui_lines[6], "Sending to PC... (", 18) == 0);
    CHECK_STR_EQ(f.world.ui_lines[7], "Please wait: buttons and HOME do not respond until this ends");
    fixture_close(&f);
}

/* ---- unchanged on the first try ---- */

static void an_unchanged_answer_says_the_pc_already_has_this_save_and_asks_nothing(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 200, "put-unchanged.json", 0, NULL);
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_UNCHANGED);
    CHECK_STR_EQ(f.result.message, "PC already has this save");
    CHECK(f.result.backup[0] == '\0');
    CHECK_INT_EQ(f.world.confirm_calls, 0);
    CHECK_INT_EQ(f.world.net_calls, 1);
    fixture_close(&f);
}

static void a_replaced_answer_asks_nothing(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 200, "put-replaced.json", 0, NULL);
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_OK);
    CHECK_INT_EQ(f.world.confirm_calls, 0);
    fixture_close(&f);
}

static void other_refusals_never_ask_to_overwrite(void)
{
    static const struct {
        long code;
        const char *contract;
    } refusals[] = {
        { 423, "error-game-running.json" }, { 503, "error-busy.json" },          { 404, "error-no-save.json" },
        { 400, "error-invalid-zip.json" },  { 401, "error-unauthorized.json" }, { 500, "error-write-failed.json" },
    };
    for (size_t i = 0; i < sizeof refusals / sizeof refusals[0]; i++) {
        conflict_fixture_t f;
        fixture_open(&f, refusals[i].code, refusals[i].contract, 0, NULL);
        run_send(&f);
        CHECK_INT_EQ(f.world.confirm_calls, 0);
        CHECK_INT_EQ(f.world.net_calls, 1);
        fixture_close(&f);
    }
}

/* ---- a 409 that is not a conflict ---- */

static void a_malformed_conflict_is_shown_as_pc_error_http_409_without_asking(void)
{
    static const char *const bodies[] = { "{}", "not json", "{\"error\":\"conflict\"}", "{\"error\":\"conflict\",\"pc_modified\":\"soon\"}" };
    for (size_t i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
        conflict_fixture_t f;
        fixture_open(&f, 409, NULL, 0, NULL);
        f.script[0].body = bodies[i];
        run_send(&f);
        CHECK_INT_EQ(f.result.kind, SEND_PC_REFUSED);
        CHECK_STR_EQ(f.result.message, "PC error: HTTP 409");
        CHECK_INT_EQ(f.world.confirm_calls, 0);
        CHECK_INT_EQ(f.world.net_calls, 1);
        fixture_close(&f);
    }
}

static void a_409_from_a_foreign_server_is_not_a_launcher_and_asks_nothing(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 0, NULL);
    f.script[0].headers = "HTTP/1.1 409 Conflict\r\n\r\n";
    run_send(&f);
    CHECK_INT_EQ(f.result.kind, SEND_PC_REFUSED);
    CHECK_STR_EQ(f.result.message, "Not a TotK Sync launcher at 192.168.1.10:8765");
    CHECK_INT_EQ(f.world.confirm_calls, 0);
    fixture_close(&f);
}

static void the_overwrite_question_is_plain_ascii_and_free_of_the_token(void)
{
    conflict_fixture_t f;
    fixture_open(&f, 409, "error-conflict.json", 0, NULL);
    the_user_answers(&f, false);
    run_send(&f);
    CHECK(test_is_ascii_text(f.world.confirm_prompt));
    CHECK_STR_NOT_CONTAINS(f.world.confirm_prompt, CONTRACT_TOKEN);
    fixture_close(&f);
}

void run_send_conflict_tests(void)
{
    RUN_TEST(a_conflict_asks_the_overwrite_question_with_both_dates);
    RUN_TEST(a_conflict_with_no_last_sync_says_never_in_the_question);
    RUN_TEST(the_first_request_of_a_send_is_never_forced);
    RUN_TEST(answering_b_sends_nothing_more_and_reports_the_cancellation);
    RUN_TEST(answering_b_does_not_read_the_save_again);
    RUN_TEST(answering_a_sends_a_second_put_to_save_with_force_1);
    RUN_TEST(the_forced_request_carries_exactly_the_same_zip_as_the_first);
    RUN_TEST(answering_a_reads_the_save_only_once_and_never_before_the_network_call_of_the_first_put);
    RUN_TEST(answering_a_and_getting_replaced_reports_the_pc_backup);
    RUN_TEST(the_question_is_asked_only_once_even_when_the_forced_put_is_refused_with_a_conflict_again);
    RUN_TEST(a_refusal_of_the_forced_put_is_shown_like_any_refusal);
    RUN_TEST(the_forced_put_answered_with_unchanged_says_the_pc_already_has_the_save);
    RUN_TEST(the_screen_shows_the_sending_step_again_for_the_forced_put);
    RUN_TEST(an_unchanged_answer_says_the_pc_already_has_this_save_and_asks_nothing);
    RUN_TEST(a_replaced_answer_asks_nothing);
    RUN_TEST(other_refusals_never_ask_to_overwrite);
    RUN_TEST(a_malformed_conflict_is_shown_as_pc_error_http_409_without_asking);
    RUN_TEST(a_409_from_a_foreign_server_is_not_a_launcher_and_asks_nothing);
    RUN_TEST(the_overwrite_question_is_plain_ascii_and_free_of_the_token);
}
