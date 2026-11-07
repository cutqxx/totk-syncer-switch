#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "log.h"
#include "retry.h"
#include "test.h"
#include "test_fs.h"
#include "test_support.h"

#define LOG_REL "switch/totk-sync/log.txt"

static void read_log(const char *root, char **text)
{
    *text = test_read_file_in(root, LOG_REL);
    CHECK(*text != NULL);
}

static void prepare_attempt(http_request_t *req, http_response_t *resp, struct tm *when)
{
    config_t cfg;
    test_make_config(&cfg);
    api_build_status_request(&cfg, req);
    http_response_init(resp);
    resp->http_code = 200;
    snprintf(resp->trace, sizeof resp->trace,
             "> GET /status HTTP/1.1\r\nHost: 192.168.1.10:8765\r\nAuthorization: Bearer " CONTRACT_TOKEN "\r\n\r\n"
             "< HTTP/1.1 200 OK\r\nX-TotkSync-Api: 1\r\n\r\n");
    memset(when, 0, sizeof *when);
    when->tm_year = 126;
    when->tm_mon = 10;
    when->tm_mday = 7;
    when->tm_hour = 9;
    when->tm_min = 5;
    when->tm_sec = 3;
}

static void log_file_never_contains_the_token_and_authorization_becomes_stars(void)
{
    char root[256];
    char *text;
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    CHECK(test_fs_make_root(root, sizeof root));
    prepare_attempt(&req, &resp, &when);
    log_sink_init(&log, root, CONTRACT_TOKEN);
    CHECK(log_append_attempt(&log, &when, &req, &resp, 1, 2));
    read_log(root, &text);
    CHECK_STR_NOT_CONTAINS(text, CONTRACT_TOKEN);
    CHECK(strstr(text, "Authorization: ***") != NULL);
    CHECK_STR_NOT_CONTAINS(text, "Bearer");
    free(text);
}

static void token_is_removed_from_url_error_text_and_other_trace_lines(void)
{
    char root[256];
    char *text;
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    CHECK(test_fs_make_root(root, sizeof root));
    prepare_attempt(&req, &resp, &when);
    snprintf(req.url, sizeof req.url, "http://192.168.1.10:8765/status?t=%s", CONTRACT_TOKEN);
    snprintf(resp.curl_error, sizeof resp.curl_error, "oops %s oops", CONTRACT_TOKEN);
    snprintf(resp.trace, sizeof resp.trace, "< X-Echo: %s\r\n", CONTRACT_TOKEN);
    log_sink_init(&log, root, CONTRACT_TOKEN);
    CHECK(log_append_attempt(&log, &when, &req, &resp, 1, 2));
    read_log(root, &text);
    CHECK_STR_NOT_CONTAINS(text, CONTRACT_TOKEN);
    CHECK(strstr(text, "oops *** oops") != NULL);
    free(text);
}

static void a_realistic_token_that_is_not_all_zeros_is_removed_too(void)
{
    char root[256];
    char *text;
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    const char *token = "Zk9-aB_qT.7x~LmN2pQ";
    CHECK(test_fs_make_root(root, sizeof root));
    prepare_attempt(&req, &resp, &when);
    snprintf(resp.trace, sizeof resp.trace, "> GET /status HTTP/1.1\r\nAuthorization: Bearer %s\r\nX-Debug: %s\r\n", token, token);
    log_sink_init(&log, root, token);
    CHECK(log_append_attempt(&log, &when, &req, &resp, 1, 2));
    read_log(root, &text);
    CHECK_STR_NOT_CONTAINS(text, token);
    free(text);
}

static void log_never_contains_the_response_body(void)
{
    char root[256];
    char *text;
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    CHECK(test_fs_make_root(root, sizeof root));
    prepare_attempt(&req, &resp, &when);
    body_append(&resp.body, "BODY-MARKER-FINGERPRINT", 23, 100);
    log_sink_init(&log, root, CONTRACT_TOKEN);
    CHECK(log_append_attempt(&log, &when, &req, &resp, 1, 2));
    read_log(root, &text);
    CHECK_STR_NOT_CONTAINS(text, "BODY-MARKER-FINGERPRINT");
    http_response_free(&resp);
    free(text);
}

static void entry_has_timestamp_method_attempt_http_code_and_url(void)
{
    char root[256];
    char *text;
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    CHECK(test_fs_make_root(root, sizeof root));
    prepare_attempt(&req, &resp, &when);
    resp.curl_code = 0;
    log_sink_init(&log, root, CONTRACT_TOKEN);
    CHECK(log_append_attempt(&log, &when, &req, &resp, 1, 2));
    read_log(root, &text);
    CHECK(strstr(text, "[2026-11-07 09:05:03] GET attempt 1/2  HTTP 200  curl 0") != NULL);
    CHECK(strstr(text, "URL: http://192.168.1.10:8765/status") != NULL);
    free(text);
}

static void entries_are_appended_not_overwritten(void)
{
    char root[256];
    char *text;
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    CHECK(test_fs_make_root(root, sizeof root));
    prepare_attempt(&req, &resp, &when);
    log_sink_init(&log, root, CONTRACT_TOKEN);
    CHECK(log_append_attempt(&log, &when, &req, &resp, 1, 2));
    CHECK(log_append_attempt(&log, &when, &req, &resp, 2, 2));
    read_log(root, &text);
    CHECK(strstr(text, "attempt 1/2") != NULL);
    CHECK(strstr(text, "attempt 2/2") != NULL);
    free(text);
}

static void log_without_a_secret_still_redacts_authorization(void)
{
    char root[256];
    char *text;
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    CHECK(test_fs_make_root(root, sizeof root));
    prepare_attempt(&req, &resp, &when);
    log_sink_init(&log, root, NULL);
    CHECK(log_append_attempt(&log, &when, &req, &resp, 1, 2));
    read_log(root, &text);
    CHECK_STR_NOT_CONTAINS(text, CONTRACT_TOKEN);
    CHECK(strstr(text, "Authorization: ***") != NULL);
    free(text);
}

static void log_text_is_ascii_for_ascii_input(void)
{
    char root[256];
    char *text;
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    CHECK(test_fs_make_root(root, sizeof root));
    prepare_attempt(&req, &resp, &when);
    log_sink_init(&log, root, CONTRACT_TOKEN);
    CHECK(log_append_attempt(&log, &when, &req, &resp, 1, 2));
    read_log(root, &text);
    for (const char *p = text; *p != '\0'; p++) {
        CHECK(*p == '\n' || *p == '\r' || (*p >= 0x20 && *p <= 0x7E));
    }
    free(text);
}

static void unwritable_log_returns_false_and_warns_exactly_once(void)
{
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    prepare_attempt(&req, &resp, &when);
    log_sink_init(&log, "build/tmp/no-such-root/nested", CONTRACT_TOKEN);
    CHECK(!log_append_attempt(&log, &when, &req, &resp, 1, 2));
    CHECK(log_take_warning(&log));
    CHECK(!log_take_warning(&log));
    CHECK(!log_append_attempt(&log, &when, &req, &resp, 2, 2));
    CHECK(!log_take_warning(&log));
}

static void no_warning_when_logging_works(void)
{
    char root[256];
    log_sink_t log;
    http_request_t req;
    http_response_t resp;
    struct tm when;
    CHECK(test_fs_make_root(root, sizeof root));
    prepare_attempt(&req, &resp, &when);
    log_sink_init(&log, root, CONTRACT_TOKEN);
    CHECK(log_append_attempt(&log, &when, &req, &resp, 1, 2));
    CHECK(!log_take_warning(&log));
}

static void retry_logs_every_attempt_with_its_number(void)
{
    const scripted_reply_t script[] = { { 0, 7, NULL, NULL }, { 200, 0, LAUNCHER_MARKER_HEADERS, "{}" } };
    char root[256];
    char *text;
    fake_world_t world;
    log_sink_t log;
    config_t cfg;
    http_request_t req;
    http_response_t out;
    CHECK(test_fs_make_root(root, sizeof root));
    fake_world_init(&world, script, 2);
    test_make_config(&cfg);
    log_sink_init(&log, root, cfg.token);
    world.env.log = &log;
    api_build_status_request(&cfg, &req);
    retry_perform(&world.env, &req, &out);
    read_log(root, &text);
    CHECK(strstr(text, "[2026-11-07 12:00:00] GET attempt 1/2  HTTP 0  curl 7") != NULL);
    CHECK(strstr(text, "GET attempt 2/2  HTTP 200  curl 0") != NULL);
    CHECK_STR_NOT_CONTAINS(text, CONTRACT_TOKEN);
    http_response_free(&out);
    free(text);
}

static void retry_tells_the_user_once_when_the_log_cannot_be_written(void)
{
    const scripted_reply_t script[] = { { 0, 7, NULL, NULL }, { 0, 7, NULL, NULL } };
    fake_world_t world;
    log_sink_t log;
    config_t cfg;
    http_request_t req;
    http_response_t out;
    int log_unavailable_lines = 0;
    fake_world_init(&world, script, 2);
    test_make_config(&cfg);
    log_sink_init(&log, "build/tmp/no-such-root/nested", cfg.token);
    world.env.log = &log;
    api_build_status_request(&cfg, &req);
    retry_perform(&world.env, &req, &out);
    for (int i = 0; i < world.ui_line_count && i < MAX_RECORDED_LINES; i++) {
        if (strcmp(world.ui_lines[i], "Log unavailable") == 0) {
            log_unavailable_lines++;
        }
    }
    CHECK_INT_EQ(log_unavailable_lines, 1);
    CHECK_INT_EQ(world.net_calls, 2);
    http_response_free(&out);
}

void run_log_tests(void)
{
    RUN_TEST(log_file_never_contains_the_token_and_authorization_becomes_stars);
    RUN_TEST(token_is_removed_from_url_error_text_and_other_trace_lines);
    RUN_TEST(a_realistic_token_that_is_not_all_zeros_is_removed_too);
    RUN_TEST(log_never_contains_the_response_body);
    RUN_TEST(entry_has_timestamp_method_attempt_http_code_and_url);
    RUN_TEST(entries_are_appended_not_overwritten);
    RUN_TEST(log_without_a_secret_still_redacts_authorization);
    RUN_TEST(log_text_is_ascii_for_ascii_input);
    RUN_TEST(unwritable_log_returns_false_and_warns_exactly_once);
    RUN_TEST(no_warning_when_logging_works);
    RUN_TEST(retry_logs_every_attempt_with_its_number);
    RUN_TEST(retry_tells_the_user_once_when_the_log_cannot_be_written);
}
