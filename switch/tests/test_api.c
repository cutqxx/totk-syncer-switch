#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "test.h"
#include "test_support.h"

#define NO_MARKER_HEADERS "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"

static void interpret_contract(const char *contract_name, long http_code, const char *headers, api_status_t *out)
{
    char path[256];
    char *body;
    http_response_t resp;
    snprintf(path, sizeof path, "%s/api", CONTRACTS_DIR);
    body = test_read_file_in(path, contract_name);
    CHECK(body != NULL);
    http_response_init(&resp);
    scripted_reply_t reply = { http_code, 0, headers, body };
    test_fill_response(&resp, &reply);
    api_interpret_status(&resp, out);
    http_response_free(&resp);
    free(body);
}

static void interpret_text(long http_code, int curl_code, const char *headers, const char *body, api_status_t *out)
{
    http_response_t resp;
    http_response_init(&resp);
    scripted_reply_t reply = { http_code, curl_code, headers, body };
    test_fill_response(&resp, &reply);
    api_interpret_status(&resp, out);
    http_response_free(&resp);
}

/* ---- request ---- */

static void status_request_is_a_get_to_http_host_port_status(void)
{
    config_t cfg;
    http_request_t req;
    test_make_config(&cfg);
    api_build_status_request(&cfg, &req);
    CHECK_INT_EQ(req.method, HTTP_GET);
    CHECK_STR_EQ(req.url, "http://192.168.1.10:8765/status");
    CHECK(req.body == NULL);
}

static void status_request_follows_the_configured_host_and_port(void)
{
    config_t cfg;
    http_request_t req;
    test_make_config(&cfg);
    snprintf(cfg.pc_host, sizeof cfg.pc_host, "10.1.2.3");
    cfg.pc_port = 1024;
    api_build_status_request(&cfg, &req);
    CHECK_STR_EQ(req.url, "http://10.1.2.3:1024/status");
}

static void status_request_carries_the_bearer_token_in_authorization(void)
{
    config_t cfg;
    http_request_t req;
    test_make_config(&cfg);
    api_build_status_request(&cfg, &req);
    CHECK_INT_EQ(req.header_count, 1);
    CHECK_STR_EQ(req.headers[0], "Authorization: Bearer " CONTRACT_TOKEN);
}

static void status_request_keeps_the_longest_token_intact(void)
{
    config_t cfg;
    http_request_t req;
    char expected[256];
    test_make_config(&cfg);
    memset(cfg.token, 'k', 127);
    cfg.token[127] = '\0';
    api_build_status_request(&cfg, &req);
    snprintf(expected, sizeof expected, "Authorization: Bearer %s", cfg.token);
    CHECK_STR_EQ(req.headers[0], expected);
}

static void status_request_uses_5s_connect_and_60s_total_timeouts(void)
{
    config_t cfg;
    http_request_t req;
    test_make_config(&cfg);
    api_build_status_request(&cfg, &req);
    CHECK_INT_EQ(req.connect_timeout_s, 5);
    CHECK_INT_EQ(req.total_timeout_s, 60);
}

/* ---- interpret: 200 ---- */

static void ok_status_with_marker_is_online_with_save_time(void)
{
    api_status_t st;
    interpret_contract("status-ok.json", 200, LAUNCHER_MARKER_HEADERS, &st);
    CHECK_INT_EQ(st.kind, API_STATUS_OK);
    CHECK(!st.game_running);
    CHECK(st.save_present);
    CHECK_STR_EQ(st.save_modified, "2026-11-03 14:25");
    CHECK_INT_EQ(st.http_code, 200);
}

static void never_synced_status_is_online_with_save_time(void)
{
    api_status_t st;
    interpret_contract("status-never-synced.json", 200, LAUNCHER_MARKER_HEADERS, &st);
    CHECK_INT_EQ(st.kind, API_STATUS_OK);
    CHECK(st.save_present);
    CHECK_STR_EQ(st.save_modified, "2026-11-03 14:25");
}

static void no_save_status_is_online_without_save(void)
{
    api_status_t st;
    interpret_contract("status-no-save.json", 200, LAUNCHER_MARKER_HEADERS, &st);
    CHECK_INT_EQ(st.kind, API_STATUS_OK);
    CHECK(!st.save_present);
    CHECK(!st.game_running);
}

static void game_running_status_reports_the_game_and_the_save(void)
{
    api_status_t st;
    interpret_contract("status-game-running.json", 200, LAUNCHER_MARKER_HEADERS, &st);
    CHECK_INT_EQ(st.kind, API_STATUS_OK);
    CHECK(st.game_running);
    CHECK(st.save_present);
}

static void marker_header_name_is_matched_in_any_letter_case(void)
{
    api_status_t st;
    interpret_contract("status-ok.json", 200, "HTTP/1.1 200 OK\r\nx-totksync-api: 1\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_OK);
    interpret_contract("status-ok.json", 200, "HTTP/1.1 200 OK\r\nX-TOTKSYNC-API:1\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_OK);
}

static void marker_header_is_found_after_an_interim_100_continue_block(void)
{
    api_status_t st;
    interpret_contract("status-ok.json", 200,
                       "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-TotkSync-Api: 1\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_OK);
}

static void ok_status_without_marker_is_not_a_launcher(void)
{
    api_status_t st;
    interpret_contract("status-ok.json", 200, NO_MARKER_HEADERS, &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
}

static void marker_with_a_different_value_is_not_a_launcher(void)
{
    api_status_t st;
    interpret_contract("status-ok.json", 200, "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 2\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
}

static void other_app_name_or_api_version_in_the_body_is_not_a_launcher(void)
{
    api_status_t st;
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS,
                   "{\"app\":\"other\",\"api\":1,\"game_running\":false,\"save\":{\"present\":false}}", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS,
                   "{\"app\":\"totk-sync\",\"api\":2,\"game_running\":false,\"save\":{\"present\":false}}", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS,
                   "{\"app\":\"totk-sync\",\"api\":\"1\",\"game_running\":false,\"save\":{\"present\":false}}", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, "{\"game_running\":false,\"save\":{\"present\":false}}", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
}

static void html_or_garbage_or_empty_200_body_is_not_a_launcher(void)
{
    api_status_t st;
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, "<html>router login</html>", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, "", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, NULL, &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, "[1,2,3]", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
}

static void launcher_200_with_malformed_status_fields_is_a_bad_response(void)
{
    api_status_t st;
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, "{\"app\":\"totk-sync\",\"api\":1,\"save\":{\"present\":false}}", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_BAD_RESPONSE);
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, "{\"app\":\"totk-sync\",\"api\":1,\"game_running\":false}", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_BAD_RESPONSE);
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS,
                   "{\"app\":\"totk-sync\",\"api\":1,\"game_running\":false,\"save\":{\"present\":true}}", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_BAD_RESPONSE);
    interpret_text(200, 0, LAUNCHER_MARKER_HEADERS,
                   "{\"app\":\"totk-sync\",\"api\":1,\"game_running\":false,\"save\":{\"present\":true,\"modified\":\"yesterday\"}}", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_BAD_RESPONSE);
}

/* ---- interpret: errors ---- */

static void unauthorized_with_marker_is_wrong_token(void)
{
    api_status_t st;
    interpret_contract("error-unauthorized.json", 401, "HTTP/1.1 401 Unauthorized\r\nX-TotkSync-Api: 1\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_WRONG_TOKEN);
    CHECK_INT_EQ(st.http_code, 401);
}

static void unauthorized_without_marker_is_not_a_launcher(void)
{
    api_status_t st;
    interpret_contract("error-unauthorized.json", 401, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
}

static void forbidden_with_marker_is_forbidden(void)
{
    api_status_t st;
    interpret_contract("error-forbidden.json", 403, "HTTP/1.1 403 Forbidden\r\nX-TotkSync-Api: 1\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_FORBIDDEN);
}

static void forbidden_without_marker_is_not_a_launcher(void)
{
    api_status_t st;
    interpret_contract("error-forbidden.json", 403, "HTTP/1.1 403 Forbidden\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
}

static void not_found_internal_and_method_not_allowed_with_marker_are_http_errors(void)
{
    api_status_t st;
    interpret_contract("error-not-found.json", 404, "HTTP/1.1 404 Not Found\r\nX-TotkSync-Api: 1\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_HTTP_ERROR);
    CHECK_INT_EQ(st.http_code, 404);
    interpret_contract("error-internal.json", 500, "HTTP/1.1 500 Internal\r\nX-TotkSync-Api: 1\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_HTTP_ERROR);
    CHECK_INT_EQ(st.http_code, 500);
    interpret_contract("error-method-not-allowed.json", 405, "HTTP/1.1 405 Method Not Allowed\r\nX-TotkSync-Api: 1\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_HTTP_ERROR);
    CHECK_INT_EQ(st.http_code, 405);
}

static void error_codes_without_marker_are_not_a_launcher(void)
{
    api_status_t st;
    interpret_contract("error-not-found.json", 404, "HTTP/1.1 404 Not Found\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
    interpret_contract("error-internal.json", 500, "HTTP/1.1 500 Internal\r\n\r\n", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NOT_LAUNCHER);
}

static void redirect_with_marker_is_an_http_error(void)
{
    api_status_t st;
    interpret_text(302, 0, "HTTP/1.1 302 Found\r\nX-TotkSync-Api: 1\r\n\r\n", NULL, &st);
    CHECK_INT_EQ(st.kind, API_STATUS_HTTP_ERROR);
    CHECK_INT_EQ(st.http_code, 302);
}

/* ---- interpret: transport ---- */

static void curl_6_7_28_are_unreachable_even_with_no_http_code(void)
{
    static const int codes[] = { 6, 7, 28 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        api_status_t st;
        interpret_text(0, codes[i], NULL, NULL, &st);
        CHECK_INT_EQ(st.kind, API_STATUS_UNREACHABLE);
        CHECK_INT_EQ(st.curl_code, codes[i]);
    }
}

static void other_curl_failures_are_network_errors_carrying_the_code(void)
{
    api_status_t st;
    interpret_text(0, 56, NULL, NULL, &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NETWORK_ERROR);
    CHECK_INT_EQ(st.curl_code, 56);
    interpret_text(0, 35, NULL, NULL, &st);
    CHECK_INT_EQ(st.kind, API_STATUS_NETWORK_ERROR);
    CHECK_INT_EQ(st.curl_code, 35);
}

static void curl_failure_wins_over_a_stale_http_code_and_marker(void)
{
    api_status_t st;
    interpret_text(200, 28, LAUNCHER_MARKER_HEADERS, "{}", &st);
    CHECK_INT_EQ(st.kind, API_STATUS_UNREACHABLE);
}

/* ---- modified time ---- */

static void modified_time_is_taken_from_the_text_without_recalculation(void)
{
    char out[API_MODIFIED_SIZE];
    CHECK(api_format_modified("2026-11-07T23:59:59+03:00", out));
    CHECK_STR_EQ(out, "2026-11-07 23:59");
    CHECK(api_format_modified("2026-11-03T14:25:01+03:00", out));
    CHECK_STR_EQ(out, "2026-11-03 14:25");
}

static void modified_time_keeps_the_local_wall_clock_for_any_offset(void)
{
    char out[API_MODIFIED_SIZE];
    CHECK(api_format_modified("2026-01-01T00:00:00-08:00", out));
    CHECK_STR_EQ(out, "2026-01-01 00:00");
    CHECK(api_format_modified("2026-12-31T23:30:00-11:30", out));
    CHECK_STR_EQ(out, "2026-12-31 23:30");
    CHECK(api_format_modified("2026-06-15T09:05:00Z", out));
    CHECK_STR_EQ(out, "2026-06-15 09:05");
    CHECK(api_format_modified("2026-06-15T09:05:00.1234567+14:00", out));
    CHECK_STR_EQ(out, "2026-06-15 09:05");
}

static void modified_time_rejects_malformed_text(void)
{
    static const char *const bad[] = {
        "", "garbage", "2026-11-07", "2026-11-07 23:59:59", "2026-11-07T2359", "2026/11/07T23:59:59",
        "26-11-07T23:59:59", "2026-11-07T23:5", "abcd-ef-ghTij:kl", " 2026-11-07T23:59:59",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char out[API_MODIFIED_SIZE] = "untouched";
        CHECK(!api_format_modified(bad[i], out));
        CHECK_STR_EQ(out, "untouched");
    }
}

static void modified_time_with_exactly_the_minimum_length_is_accepted(void)
{
    char out[API_MODIFIED_SIZE];
    CHECK(api_format_modified("2026-11-07T23:59", out));
    CHECK_STR_EQ(out, "2026-11-07 23:59");
}

void run_api_tests(void)
{
    RUN_TEST(status_request_is_a_get_to_http_host_port_status);
    RUN_TEST(status_request_follows_the_configured_host_and_port);
    RUN_TEST(status_request_carries_the_bearer_token_in_authorization);
    RUN_TEST(status_request_keeps_the_longest_token_intact);
    RUN_TEST(status_request_uses_5s_connect_and_60s_total_timeouts);
    RUN_TEST(ok_status_with_marker_is_online_with_save_time);
    RUN_TEST(never_synced_status_is_online_with_save_time);
    RUN_TEST(no_save_status_is_online_without_save);
    RUN_TEST(game_running_status_reports_the_game_and_the_save);
    RUN_TEST(marker_header_name_is_matched_in_any_letter_case);
    RUN_TEST(marker_header_is_found_after_an_interim_100_continue_block);
    RUN_TEST(ok_status_without_marker_is_not_a_launcher);
    RUN_TEST(marker_with_a_different_value_is_not_a_launcher);
    RUN_TEST(other_app_name_or_api_version_in_the_body_is_not_a_launcher);
    RUN_TEST(html_or_garbage_or_empty_200_body_is_not_a_launcher);
    RUN_TEST(launcher_200_with_malformed_status_fields_is_a_bad_response);
    RUN_TEST(unauthorized_with_marker_is_wrong_token);
    RUN_TEST(unauthorized_without_marker_is_not_a_launcher);
    RUN_TEST(forbidden_with_marker_is_forbidden);
    RUN_TEST(forbidden_without_marker_is_not_a_launcher);
    RUN_TEST(not_found_internal_and_method_not_allowed_with_marker_are_http_errors);
    RUN_TEST(error_codes_without_marker_are_not_a_launcher);
    RUN_TEST(redirect_with_marker_is_an_http_error);
    RUN_TEST(curl_6_7_28_are_unreachable_even_with_no_http_code);
    RUN_TEST(other_curl_failures_are_network_errors_carrying_the_code);
    RUN_TEST(curl_failure_wins_over_a_stale_http_code_and_marker);
    RUN_TEST(modified_time_is_taken_from_the_text_without_recalculation);
    RUN_TEST(modified_time_keeps_the_local_wall_clock_for_any_offset);
    RUN_TEST(modified_time_rejects_malformed_text);
    RUN_TEST(modified_time_with_exactly_the_minimum_length_is_accepted);
}
