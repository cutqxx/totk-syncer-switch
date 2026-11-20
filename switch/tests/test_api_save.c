#include <stdlib.h>
#include <string.h>

#include "api_save.h"
#include "test.h"
#include "test_support.h"

#define FINGERPRINT "sha256:e19ed4007a1fcdcc231a1c9bb8e9f73753913c0db5328afc635f94c12f6f4a5e"
#define ZIP_HEADERS                                                                                                      \
    "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 1\r\nContent-Type: application/zip\r\nContent-Length: 4\r\n"                    \
    "X-TotkSync-Fingerprint: " FINGERPRINT "\r\nX-TotkSync-Modified: 2026-11-03T14:25:01+03:00\r\n\r\n"

static void interpret(long http_code, int curl_code, const char *headers, const char *body, api_get_result_t *out)
{
    http_response_t resp;
    scripted_reply_t reply = { http_code, curl_code, headers, body };
    http_response_init(&resp);
    test_fill_response(&resp, &reply);
    api_interpret_get_save(&resp, out);
    http_response_free(&resp);
}

static void get_save_request_has_the_url_token_and_room_for_a_whole_save(void)
{
    config_t cfg;
    http_request_t req;
    test_make_config(&cfg);

    api_build_get_save_request(&cfg, &req);

    CHECK_INT_EQ(req.method, HTTP_GET);
    CHECK_STR_EQ(req.url, "http://192.168.1.10:8765/save");
    CHECK_STR_EQ(req.headers[0], "Authorization: Bearer " CONTRACT_TOKEN);
    CHECK_INT_EQ(req.max_response_bytes, 64LL * 1024 * 1024);
    CHECK_INT_EQ(req.total_timeout_s, 900);
    CHECK(req.body == NULL);
}

static void a_zip_with_both_headers_is_ok_and_keeps_the_fingerprint_and_date(void)
{
    api_get_result_t out;
    interpret(200, 0, ZIP_HEADERS, "PK..", &out);
    CHECK_INT_EQ(out.kind, API_GET_OK);
    CHECK_STR_EQ(out.fingerprint, FINGERPRINT);
    CHECK_STR_EQ(out.modified, "2026-11-03 14:25");
}

static void a_200_without_a_valid_header_or_body_is_a_bad_response(void)
{
    static const char *const headers[] = {
        "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 1\r\nContent-Type: application/zip\r\n"
        "X-TotkSync-Modified: 2026-11-03T14:25:01+03:00\r\n\r\n",
        "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 1\r\nContent-Type: application/zip\r\nX-TotkSync-Fingerprint: " FINGERPRINT "\r\n\r\n",
        "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 1\r\nContent-Type: application/json\r\nX-TotkSync-Fingerprint: " FINGERPRINT
        "\r\nX-TotkSync-Modified: 2026-11-03T14:25:01+03:00\r\n\r\n",
        "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 1\r\nContent-Type: application/zip\r\nX-TotkSync-Fingerprint: sha256:ABC\r\n"
        "X-TotkSync-Modified: 2026-11-03T14:25:01+03:00\r\n\r\n",
        "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 1\r\nContent-Type: application/zip\r\nX-TotkSync-Fingerprint: " FINGERPRINT
        "\r\nX-TotkSync-Modified: yesterday\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof headers / sizeof headers[0]; i++) {
        api_get_result_t out;
        interpret(200, 0, headers[i], "PK..", &out);
        CHECK_INT_EQ(out.kind, API_GET_BAD_RESPONSE);
    }
    api_get_result_t empty;
    interpret(200, 0, ZIP_HEADERS, NULL, &empty);
    CHECK_INT_EQ(empty.kind, API_GET_BAD_RESPONSE);
}

static void refusals_from_the_launcher_are_told_apart(void)
{
    char *no_save = test_read_file_in(CONTRACTS_DIR "/api", "error-no-save.json");
    char *game = test_read_file_in(CONTRACTS_DIR "/api", "error-game-running.json");
    char *busy = test_read_file_in(CONTRACTS_DIR "/api", "error-busy.json");
    api_get_result_t out;

    interpret(404, 0, LAUNCHER_MARKER_HEADERS, no_save, &out);
    CHECK_INT_EQ(out.kind, API_GET_NO_SAVE);
    interpret(423, 0, LAUNCHER_MARKER_HEADERS, game, &out);
    CHECK_INT_EQ(out.kind, API_GET_GAME_RUNNING);
    interpret(503, 0, LAUNCHER_MARKER_HEADERS, busy, &out);
    CHECK_INT_EQ(out.kind, API_GET_BUSY);
    interpret(401, 0, LAUNCHER_MARKER_HEADERS, "{}", &out);
    CHECK_INT_EQ(out.kind, API_GET_WRONG_TOKEN);
    interpret(403, 0, LAUNCHER_MARKER_HEADERS, "{}", &out);
    CHECK_INT_EQ(out.kind, API_GET_FORBIDDEN);
    interpret(500, 0, LAUNCHER_MARKER_HEADERS, "{\"error\":\"read_failed\",\"reason\":\"couldn't read the PC save: locked\"}", &out);
    CHECK_INT_EQ(out.kind, API_GET_READ_FAILED);
    CHECK_STR_EQ(out.reason, "couldn't read the PC save: locked");
    interpret(500, 0, LAUNCHER_MARKER_HEADERS, "{\"error\":\"internal\"}", &out);
    CHECK_INT_EQ(out.kind, API_GET_HTTP_ERROR);
    interpret(404, 0, LAUNCHER_MARKER_HEADERS, "{\"error\":\"not_found\"}", &out);
    CHECK_INT_EQ(out.kind, API_GET_HTTP_ERROR);
    free(no_save);
    free(game);
    free(busy);
}

static void transport_failures_and_foreign_servers_are_classified(void)
{
    api_get_result_t out;
    interpret(0, 7, NULL, NULL, &out);
    CHECK_INT_EQ(out.kind, API_GET_UNREACHABLE);
    interpret(0, 28, NULL, NULL, &out);
    CHECK_INT_EQ(out.kind, API_GET_UNREACHABLE);
    interpret(0, 56, NULL, NULL, &out);
    CHECK_INT_EQ(out.kind, API_GET_NETWORK_ERROR);
    interpret(200, 0, "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\n\r\n", "PK..", &out);
    CHECK_INT_EQ(out.kind, API_GET_NOT_LAUNCHER);
}

static void ack_request_posts_the_fingerprint_as_json(void)
{
    config_t cfg;
    http_request_t req;
    body_t body = { NULL, 0 };
    test_make_config(&cfg);

    CHECK(api_build_ack_request(&cfg, FINGERPRINT, &body, &req));

    CHECK_INT_EQ(req.method, HTTP_POST);
    CHECK_STR_EQ(req.url, "http://192.168.1.10:8765/save/ack");
    CHECK_STR_EQ(req.headers[0], "Authorization: Bearer " CONTRACT_TOKEN);
    CHECK_STR_EQ(req.headers[1], "Content-Type: application/json");
    CHECK(req.body == &body);
    CHECK_STR_EQ((const char *)body.data, "{\"fingerprint\":\"" FINGERPRINT "\"}");
    body_free(&body);
}

static void ack_request_refuses_a_malformed_fingerprint(void)
{
    config_t cfg;
    http_request_t req;
    body_t body = { NULL, 0 };
    test_make_config(&cfg);
    CHECK(!api_build_ack_request(&cfg, "sha256:\"}{evil", &body, &req));
    CHECK(!api_build_ack_request(&cfg, "", &body, &req));
    CHECK(body.data == NULL);
}

static void only_a_recorded_answer_from_the_launcher_confirms_the_sync(void)
{
    char *recorded = test_read_file_in(CONTRACTS_DIR "/api", "ack-recorded.json");
    char *mismatch = test_read_file_in(CONTRACTS_DIR "/api", "error-fingerprint-mismatch.json");
    http_response_t resp;
    scripted_reply_t ok = { 200, 0, LAUNCHER_MARKER_HEADERS, recorded };
    scripted_reply_t conflict = { 409, 0, LAUNCHER_MARKER_HEADERS, mismatch };
    scripted_reply_t foreign = { 200, 0, "HTTP/1.1 200 OK\r\n\r\n", recorded };
    scripted_reply_t dropped = { 0, 7, NULL, NULL };
    const scripted_reply_t *cases[] = { &ok, &conflict, &foreign, &dropped };
    const bool expected[] = { true, false, false, false };
    CHECK(recorded != NULL && mismatch != NULL);
    for (size_t i = 0; i < 4; i++) {
        http_response_init(&resp);
        test_fill_response(&resp, cases[i]);
        CHECK(api_interpret_ack(&resp) == expected[i]);
        http_response_free(&resp);
    }
    free(recorded);
    free(mismatch);
}

static void header_lookup_takes_the_last_value_case_insensitively(void)
{
    http_response_t resp;
    char value[64];
    http_response_init(&resp);
    snprintf(resp.response_headers, sizeof resp.response_headers,
             "HTTP/1.1 100 Continue\r\nx-thing: first\r\n\r\nHTTP/1.1 200 OK\r\nX-Thing:   second  \r\n\r\n");
    CHECK(http_response_header_value(&resp, "X-THING", value, sizeof value));
    CHECK_STR_EQ(value, "second");
    CHECK(!http_response_header_value(&resp, "X-Other", value, sizeof value));
    CHECK_STR_EQ(value, "");
}

void run_api_save_tests(void)
{
    RUN_TEST(get_save_request_has_the_url_token_and_room_for_a_whole_save);
    RUN_TEST(a_zip_with_both_headers_is_ok_and_keeps_the_fingerprint_and_date);
    RUN_TEST(a_200_without_a_valid_header_or_body_is_a_bad_response);
    RUN_TEST(refusals_from_the_launcher_are_told_apart);
    RUN_TEST(transport_failures_and_foreign_servers_are_classified);
    RUN_TEST(ack_request_posts_the_fingerprint_as_json);
    RUN_TEST(ack_request_refuses_a_malformed_fingerprint);
    RUN_TEST(only_a_recorded_answer_from_the_launcher_confirms_the_sync);
    RUN_TEST(header_lookup_takes_the_last_value_case_insensitively);
}
