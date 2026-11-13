#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api_put.h"
#include "send_to_pc.h"
#include "test.h"
#include "test_support.h"

#define NO_MARKER_HEADERS "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
#define CONTRACT_BACKUP "pc-20261103-142501-before-switch.zip"
#define CONTRACT_FINGERPRINT "sha256:e19ed4007a1fcdcc231a1c9bb8e9f73753913c0db5328afc635f94c12f6f4a5e"
#define MEBIBYTE (1024LL * 1024)

static api_put_result_t interpret_text(long http_code, int curl_code, const char *headers, const char *body, int64_t bytes_sent)
{
    http_response_t resp;
    api_put_result_t result;
    http_response_init(&resp);
    scripted_reply_t reply = { http_code, curl_code, headers, body };
    test_fill_response(&resp, &reply);
    resp.bytes_sent = bytes_sent;
    api_interpret_put(&resp, &result);
    http_response_free(&resp);
    return result;
}

static api_put_result_t interpret_contract(const char *contract_name, long http_code, int curl_code, const char *headers,
                                           int64_t bytes_sent)
{
    char *body = test_read_file_in(CONTRACTS_DIR "/api", contract_name);
    CHECK(body != NULL);
    api_put_result_t result = interpret_text(http_code, curl_code, headers, body != NULL ? body : "", bytes_sent);
    free(body);
    return result;
}

static void describe(const api_put_result_t *put, char *out, size_t out_size)
{
    config_t cfg;
    test_make_config(&cfg);
    send_describe_put(put, &cfg, out, out_size);
}

/* ---- the request ---- */

static void put_request_is_a_put_to_http_host_port_save(void)
{
    config_t cfg;
    http_request_t req;
    body_t zip = { NULL, 0 };
    test_make_config(&cfg);
    api_build_put_request(&cfg, &zip, &req);
    CHECK_INT_EQ(req.method, HTTP_PUT);
    CHECK_STR_EQ(req.url, "http://192.168.1.10:8765/save");
}

static void put_request_follows_the_configured_host_and_port(void)
{
    config_t cfg;
    http_request_t req;
    body_t zip = { NULL, 0 };
    test_make_config(&cfg);
    snprintf(cfg.pc_host, sizeof cfg.pc_host, "10.1.2.3");
    cfg.pc_port = 1024;
    api_build_put_request(&cfg, &zip, &req);
    CHECK_STR_EQ(req.url, "http://10.1.2.3:1024/save");
}

static void put_request_has_exactly_the_authorization_content_type_and_expect_headers(void)
{
    config_t cfg;
    http_request_t req;
    body_t zip = { NULL, 0 };
    test_make_config(&cfg);
    api_build_put_request(&cfg, &zip, &req);
    CHECK_INT_EQ(req.header_count, 3);
    CHECK_STR_EQ(req.headers[0], "Authorization: Bearer " CONTRACT_TOKEN);
    CHECK_STR_EQ(req.headers[1], "Content-Type: application/zip");
    CHECK_STR_EQ(req.headers[2], "Expect: 100-continue");
}

static void the_token_appears_only_in_the_authorization_header(void)
{
    config_t cfg;
    http_request_t req;
    body_t zip = { NULL, 0 };
    test_make_config(&cfg);
    api_build_put_request(&cfg, &zip, &req);
    CHECK_STR_NOT_CONTAINS(req.url, CONTRACT_TOKEN);
    CHECK_STR_NOT_CONTAINS(req.headers[1], CONTRACT_TOKEN);
    CHECK_STR_NOT_CONTAINS(req.headers[2], CONTRACT_TOKEN);
}

static void put_request_waits_three_seconds_for_100_continue(void)
{
    config_t cfg;
    http_request_t req;
    body_t zip = { NULL, 0 };
    test_make_config(&cfg);
    api_build_put_request(&cfg, &zip, &req);
    CHECK_INT_EQ(req.expect_100_timeout_ms, 3000);
}

static void put_request_hands_over_the_zip_so_the_length_is_known(void)
{
    config_t cfg;
    http_request_t req;
    unsigned char bytes[] = { 'P', 'K', 3, 4, 0 };
    body_t zip = { bytes, 4 };
    test_make_config(&cfg);
    api_build_put_request(&cfg, &zip, &req);
    CHECK(req.body == &zip);
    CHECK_INT_EQ(req.body->size, 4);
}

static void put_request_has_a_5_second_connect_timeout_and_keeps_the_json_response_limit(void)
{
    config_t cfg;
    http_request_t req;
    body_t zip = { NULL, 0 };
    test_make_config(&cfg);
    api_build_put_request(&cfg, &zip, &req);
    CHECK_INT_EQ(req.connect_timeout_s, 5);
    CHECK_INT_EQ(req.max_response_bytes, TS_MAX_JSON_BYTES);
}

static void the_total_timeout_is_30_seconds_plus_the_time_at_64_kib_per_second(void)
{
    CHECK_INT_EQ(api_put_timeout_s(0), 30);
    CHECK_INT_EQ(api_put_timeout_s(1), 31);
    CHECK_INT_EQ(api_put_timeout_s(64 * 1024), 31);
    CHECK_INT_EQ(api_put_timeout_s(64 * 1024 + 1), 32);
    CHECK_INT_EQ(api_put_timeout_s(1 * MEBIBYTE), 46);
    CHECK_INT_EQ(api_put_timeout_s(20 * MEBIBYTE), 350);
    CHECK_INT_EQ(api_put_timeout_s(20000000), 336);
    CHECK_INT_EQ(api_put_timeout_s(40 * MEBIBYTE), 30 + 640);
    CHECK_INT_EQ(api_put_timeout_s(64 * MEBIBYTE), 900);
}

static void the_total_timeout_never_exceeds_900_seconds(void)
{
    CHECK_INT_EQ(api_put_timeout_s(870 * 64 * 1024), 900);
    CHECK_INT_EQ(api_put_timeout_s(870 * 64 * 1024 + 1), 900);
    CHECK_INT_EQ(api_put_timeout_s(1LL << 40), 900);
    CHECK_INT_EQ(api_put_timeout_s(INT64_MAX / 2), 900);
}

static void the_request_total_timeout_follows_the_zip_size(void)
{
    config_t cfg;
    http_request_t req;
    unsigned char byte = 0;
    body_t zip = { &byte, 20 * MEBIBYTE };
    test_make_config(&cfg);
    api_build_put_request(&cfg, &zip, &req);
    CHECK_INT_EQ(req.total_timeout_s, 350);
}

/* ---- the answer ---- */

static void a_replaced_answer_is_read_from_the_contract_body(void)
{
    api_put_result_t put = interpret_contract("put-replaced.json", 200, 0, LAUNCHER_MARKER_HEADERS, 5000);
    CHECK_INT_EQ(put.kind, API_PUT_REPLACED);
    CHECK_STR_EQ(put.backup, CONTRACT_BACKUP);
    CHECK_STR_EQ(put.fingerprint, CONTRACT_FINGERPRINT);
    CHECK_INT_EQ(put.http_code, 200);
    CHECK_INT_EQ(put.curl_code, 0);
    CHECK_INT_EQ(put.bytes_sent, 5000);
}

static void a_200_body_that_is_not_a_replaced_result_is_an_unexpected_answer(void)
{
    static const char *const bodies[] = {
        "",
        "not json",
        "{",
        "[]",
        "\"replaced\"",
        "{}",
        "{\"result\":\"replaced\",\"fingerprint\":\"" CONTRACT_FINGERPRINT "\"}",
        "{\"result\":\"replaced\",\"backup\":\"" CONTRACT_BACKUP "\"}",
        "{\"result\":1,\"backup\":\"" CONTRACT_BACKUP "\",\"fingerprint\":\"" CONTRACT_FINGERPRINT "\"}",
        "{\"result\":\"REPLACED\",\"backup\":\"" CONTRACT_BACKUP "\",\"fingerprint\":\"" CONTRACT_FINGERPRINT "\"}",
        "{\"result\":\"replaced\",\"backup\":7,\"fingerprint\":\"" CONTRACT_FINGERPRINT "\"}",
        "{\"result\":\"replaced\",\"backup\":\"" CONTRACT_BACKUP "\",\"fingerprint\":7}",
        "{\"app\":\"totk-sync\",\"api\":1,\"game_running\":false}",
    };
    for (size_t i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
        api_put_result_t put = interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, bodies[i], 100);
        if (put.kind != API_PUT_BAD_RESPONSE) {
            printf("  body %zu gave kind %d\n", i, (int)put.kind);
        }
        CHECK_INT_EQ(put.kind, API_PUT_BAD_RESPONSE);
    }
}

static void a_200_without_a_body_at_all_is_an_unexpected_answer(void)
{
    api_put_result_t put = interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, NULL, 100);
    CHECK_INT_EQ(put.kind, API_PUT_BAD_RESPONSE);
}

static void a_fingerprint_that_is_not_sha256_and_64_lowercase_hex_is_an_unexpected_answer(void)
{
    static const char *const fingerprints[] = {
        "e19ed4007a1fcdcc231a1c9bb8e9f73753913c0db5328afc635f94c12f6f4a5e",
        "sha256:e19ed4007a1fcdcc231a1c9bb8e9f73753913c0db5328afc635f94c12f6f4a5",
        "sha256:e19ed4007a1fcdcc231a1c9bb8e9f73753913c0db5328afc635f94c12f6f4a5ee",
        "sha256:E19ED4007A1FCDCC231A1C9BB8E9F73753913C0DB5328AFC635F94C12F6F4A5E",
        "sha256:g19ed4007a1fcdcc231a1c9bb8e9f73753913c0db5328afc635f94c12f6f4a5e",
        "sha1:e19ed4007a1fcdcc231a1c9bb8e9f73753913c0db5328afc635f94c12f6f4a5e",
        "sha256:",
        "",
    };
    for (size_t i = 0; i < sizeof fingerprints / sizeof fingerprints[0]; i++) {
        char body[256];
        snprintf(body, sizeof body, "{\"result\":\"replaced\",\"backup\":\"" CONTRACT_BACKUP "\",\"fingerprint\":\"%s\"}", fingerprints[i]);
        api_put_result_t put = interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, body, 100);
        CHECK_INT_EQ(put.kind, API_PUT_BAD_RESPONSE);
    }
}

static void a_backup_name_that_could_point_outside_the_backup_folder_is_an_unexpected_answer(void)
{
    static const char *const backups[] = { "", "a/b.zip", "a\\b.zip", "with space.zip", "tab\t.zip", "caf\\u00e9.zip", "ctl\\u0001.zip" };
    for (size_t i = 0; i < sizeof backups / sizeof backups[0]; i++) {
        char body[512];
        snprintf(body, sizeof body, "{\"result\":\"replaced\",\"backup\":\"%s\",\"fingerprint\":\"" CONTRACT_FINGERPRINT "\"}", backups[i]);
        api_put_result_t put = interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, body, 100);
        if (put.kind != API_PUT_BAD_RESPONSE) {
            printf("  backup \"%s\" gave kind %d\n", backups[i], (int)put.kind);
        }
        CHECK_INT_EQ(put.kind, API_PUT_BAD_RESPONSE);
    }
}

static void a_backup_name_of_128_characters_is_too_long_and_127_is_fine(void)
{
    char body[512];
    char name[160];
    memset(name, 'b', 127);
    name[127] = '\0';
    snprintf(body, sizeof body, "{\"result\":\"replaced\",\"backup\":\"%s\",\"fingerprint\":\"" CONTRACT_FINGERPRINT "\"}", name);
    CHECK_INT_EQ(interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, body, 1).kind, API_PUT_REPLACED);
    memset(name, 'b', 128);
    name[128] = '\0';
    snprintf(body, sizeof body, "{\"result\":\"replaced\",\"backup\":\"%s\",\"fingerprint\":\"" CONTRACT_FINGERPRINT "\"}", name);
    CHECK_INT_EQ(interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, body, 1).kind, API_PUT_BAD_RESPONSE);
}

static void a_wrong_token_is_a_401_from_a_launcher(void)
{
    api_put_result_t put = interpret_contract("error-unauthorized.json", 401, 0, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_WRONG_TOKEN);
}

static void a_403_from_a_launcher_is_forbidden(void)
{
    api_put_result_t put = interpret_contract("error-forbidden.json", 403, 0, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_FORBIDDEN);
}

static void any_answer_without_the_marker_is_not_a_launcher(void)
{
    static const long codes[] = { 200, 400, 401, 403, 404, 423, 500, 503 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        api_put_result_t with_launcher_body = interpret_contract("put-replaced.json", codes[i], 0, NO_MARKER_HEADERS, 0);
        api_put_result_t without_headers = interpret_text(codes[i], 0, NULL, "{}", 0);
        CHECK_INT_EQ(with_launcher_body.kind, API_PUT_NOT_LAUNCHER);
        CHECK_INT_EQ(without_headers.kind, API_PUT_NOT_LAUNCHER);
    }
}

static void the_marker_must_have_the_value_1(void)
{
    api_put_result_t put = interpret_contract("put-replaced.json", 200, 0, "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 2\r\n\r\n", 0);
    CHECK_INT_EQ(put.kind, API_PUT_NOT_LAUNCHER);
}

static void an_invalid_zip_answer_carries_the_reason_of_the_pc(void)
{
    api_put_result_t put = interpret_contract("error-invalid-zip.json", 400, 0, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_INVALID_ZIP);
    CHECK_STR_EQ(put.reason, "missing option.sav");
}

static void an_invalid_zip_reason_is_made_printable_and_cut_to_fit(void)
{
    char body[512];
    char long_reason[300];
    api_put_result_t put = interpret_text(400, 0, LAUNCHER_MARKER_HEADERS, "{\"error\":\"invalid_zip\",\"reason\":\"unsafe path: caf\\u00e9\\n\"}", 0);
    CHECK_INT_EQ(put.kind, API_PUT_INVALID_ZIP);
    CHECK(test_is_ascii_text(put.reason));
    CHECK_STR_EQ(put.reason, "unsafe path: caf???");
    memset(long_reason, 'r', sizeof long_reason - 1);
    long_reason[sizeof long_reason - 1] = '\0';
    snprintf(body, sizeof body, "{\"error\":\"invalid_zip\",\"reason\":\"%s\"}", long_reason);
    put = interpret_text(400, 0, LAUNCHER_MARKER_HEADERS, body, 0);
    CHECK_INT_EQ(strlen(put.reason), API_PUT_REASON_SIZE - 1);
}

static void an_invalid_zip_answer_without_a_reason_says_no_details(void)
{
    api_put_result_t put = interpret_text(400, 0, LAUNCHER_MARKER_HEADERS, "{\"error\":\"invalid_zip\"}", 0);
    CHECK_INT_EQ(put.kind, API_PUT_INVALID_ZIP);
    CHECK_STR_EQ(put.reason, "no details");
}

static void a_400_that_is_not_an_invalid_zip_document_is_a_plain_http_error(void)
{
    api_put_result_t other_error = interpret_contract("error-not-found.json", 400, 0, LAUNCHER_MARKER_HEADERS, 0);
    api_put_result_t broken = interpret_text(400, 0, LAUNCHER_MARKER_HEADERS, "<html>", 0);
    api_put_result_t empty = interpret_text(400, 0, LAUNCHER_MARKER_HEADERS, NULL, 0);
    CHECK_INT_EQ(other_error.kind, API_PUT_HTTP_ERROR);
    CHECK_INT_EQ(broken.kind, API_PUT_HTTP_ERROR);
    CHECK_INT_EQ(empty.kind, API_PUT_HTTP_ERROR);
    CHECK_INT_EQ(other_error.http_code, 400);
}

static void a_423_means_the_game_runs_on_the_pc(void)
{
    api_put_result_t put = interpret_contract("error-game-running.json", 423, 0, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_GAME_RUNNING);
}

static void a_503_means_the_pc_is_busy(void)
{
    api_put_result_t put = interpret_contract("error-busy.json", 503, 0, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_BUSY);
}

static void a_404_no_save_document_means_the_pc_has_no_save(void)
{
    api_put_result_t put = interpret_contract("error-no-save.json", 404, 0, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_NO_SAVE);
}

static void a_404_for_an_unknown_path_is_a_plain_http_error(void)
{
    api_put_result_t put = interpret_contract("error-not-found.json", 404, 0, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_HTTP_ERROR);
    CHECK_INT_EQ(put.http_code, 404);
}

static void a_500_write_failed_carries_the_reason_that_names_the_backup(void)
{
    api_put_result_t put = interpret_contract("error-write-failed.json", 500, 0, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_WRITE_FAILED);
    CHECK_STR_EQ(put.reason, "backup failed, PC save not changed: <system message>");
}

static void a_500_internal_is_a_plain_http_error(void)
{
    api_put_result_t put = interpret_contract("error-internal.json", 500, 0, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_HTTP_ERROR);
    CHECK_INT_EQ(put.http_code, 500);
}

static void other_codes_from_a_launcher_are_plain_http_errors(void)
{
    static const struct {
        long code;
        const char *contract;
    } cases[] = {
        { 411, "error-length-required.json" }, { 413, "error-too-large.json" }, { 405, "error-method-not-allowed.json" },
        { 409, "error-internal.json" },        { 302, "error-internal.json" },  { 100, "error-internal.json" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        api_put_result_t put = interpret_contract(cases[i].contract, cases[i].code, 0, LAUNCHER_MARKER_HEADERS, 0);
        CHECK_INT_EQ(put.kind, API_PUT_HTTP_ERROR);
        CHECK_INT_EQ(put.http_code, cases[i].code);
    }
}

static void the_unreachable_curl_codes_are_6_7_and_28(void)
{
    static const int codes[] = { 6, 7, 28 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        api_put_result_t put = interpret_text(0, codes[i], NULL, NULL, 0);
        CHECK_INT_EQ(put.kind, API_PUT_UNREACHABLE);
        CHECK_INT_EQ(put.curl_code, codes[i]);
    }
}

static void other_curl_failures_without_an_answer_are_network_errors(void)
{
    static const int codes[] = { 35, 52, 55, 56, 60, -1 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        api_put_result_t put = interpret_text(0, codes[i], NULL, NULL, 0);
        CHECK_INT_EQ(put.kind, API_PUT_NETWORK_ERROR);
    }
}

static void only_curl_55_and_56_after_a_refusal_code_keep_the_refusal(void)
{
    api_put_result_t timed_out_after_423 = interpret_contract("error-game-running.json", 423, 28, LAUNCHER_MARKER_HEADERS, 0);
    api_put_result_t connect_failed_after_423 = interpret_contract("error-game-running.json", 423, 7, LAUNCHER_MARKER_HEADERS, 0);
    api_put_result_t other_error_after_423 = interpret_contract("error-game-running.json", 423, 35, LAUNCHER_MARKER_HEADERS, 0);
    CHECK_INT_EQ(timed_out_after_423.kind, API_PUT_UNREACHABLE);
    CHECK_INT_EQ(connect_failed_after_423.kind, API_PUT_UNREACHABLE);
    CHECK_INT_EQ(other_error_after_423.kind, API_PUT_NETWORK_ERROR);
}

static void a_refusal_that_arrived_before_the_connection_broke_counts_as_the_refusal(void)
{
    static const int broken_codes[] = { 55, 56 };
    for (size_t i = 0; i < sizeof broken_codes / sizeof broken_codes[0]; i++) {
        int curl_code = broken_codes[i];
        CHECK_INT_EQ(interpret_contract("error-game-running.json", 423, curl_code, LAUNCHER_MARKER_HEADERS, 0).kind, API_PUT_GAME_RUNNING);
        CHECK_INT_EQ(interpret_contract("error-busy.json", 503, curl_code, LAUNCHER_MARKER_HEADERS, 0).kind, API_PUT_BUSY);
        CHECK_INT_EQ(interpret_contract("error-no-save.json", 404, curl_code, LAUNCHER_MARKER_HEADERS, 0).kind, API_PUT_NO_SAVE);
        CHECK_INT_EQ(interpret_contract("error-unauthorized.json", 401, curl_code, LAUNCHER_MARKER_HEADERS, 0).kind, API_PUT_WRONG_TOKEN);
        CHECK_INT_EQ(interpret_contract("error-forbidden.json", 403, curl_code, LAUNCHER_MARKER_HEADERS, 0).kind, API_PUT_FORBIDDEN);
        CHECK_INT_EQ(interpret_contract("error-invalid-zip.json", 400, curl_code, LAUNCHER_MARKER_HEADERS, 0).kind, API_PUT_INVALID_ZIP);
        CHECK_INT_EQ(interpret_contract("error-write-failed.json", 500, curl_code, LAUNCHER_MARKER_HEADERS, 0).kind, API_PUT_WRITE_FAILED);
        CHECK_INT_EQ(interpret_contract("error-too-large.json", 413, curl_code, LAUNCHER_MARKER_HEADERS, 0).kind, API_PUT_HTTP_ERROR);
        CHECK_INT_EQ(interpret_contract("error-length-required.json", 411, curl_code, LAUNCHER_MARKER_HEADERS, 0).kind, API_PUT_HTTP_ERROR);
    }
}

static void a_refusal_from_a_foreign_server_before_the_connection_broke_is_not_a_launcher(void)
{
    api_put_result_t put = interpret_contract("error-game-running.json", 423, 56, NO_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_NOT_LAUNCHER);
}

static void a_broken_connection_after_a_success_code_is_a_network_error_not_a_success(void)
{
    static const long codes[] = { 0, 100, 200, 399 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        api_put_result_t put = interpret_contract("put-replaced.json", codes[i], 56, LAUNCHER_MARKER_HEADERS, 4096);
        CHECK_INT_EQ(put.kind, API_PUT_NETWORK_ERROR);
    }
}

/* ---- what the user reads ---- */

static void each_answer_has_the_exact_text_from_the_spec(void)
{
    char text[TS_LINE_SIZE];
    api_put_result_t replaced = interpret_contract("put-replaced.json", 200, 0, LAUNCHER_MARKER_HEADERS, 100);
    api_put_result_t invalid_zip = interpret_contract("error-invalid-zip.json", 400, 0, LAUNCHER_MARKER_HEADERS, 100);
    api_put_result_t game_running = interpret_contract("error-game-running.json", 423, 0, LAUNCHER_MARKER_HEADERS, 100);
    api_put_result_t busy = interpret_contract("error-busy.json", 503, 0, LAUNCHER_MARKER_HEADERS, 100);
    api_put_result_t no_save = interpret_contract("error-no-save.json", 404, 0, LAUNCHER_MARKER_HEADERS, 100);
    api_put_result_t write_failed = interpret_contract("error-write-failed.json", 500, 0, LAUNCHER_MARKER_HEADERS, 100);
    api_put_result_t wrong_token = interpret_contract("error-unauthorized.json", 401, 0, LAUNCHER_MARKER_HEADERS, 0);
    api_put_result_t forbidden = interpret_contract("error-forbidden.json", 403, 0, LAUNCHER_MARKER_HEADERS, 0);
    api_put_result_t not_launcher = interpret_contract("error-unauthorized.json", 401, 0, NO_MARKER_HEADERS, 0);
    api_put_result_t length_required = interpret_contract("error-length-required.json", 411, 0, LAUNCHER_MARKER_HEADERS, 0);
    api_put_result_t too_large = interpret_contract("error-too-large.json", 413, 0, LAUNCHER_MARKER_HEADERS, 0);
    api_put_result_t internal = interpret_contract("error-internal.json", 500, 0, LAUNCHER_MARKER_HEADERS, 0);
    api_put_result_t unexpected = interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, "{}", 100);

    describe(&replaced, text, sizeof text);
    CHECK_STR_EQ(text, "SENT TO PC. PC backup: pc-20261103-142501-before-switch.zip");
    describe(&invalid_zip, text, sizeof text);
    CHECK_STR_EQ(text, "PC rejected the save: missing option.sav");
    describe(&game_running, text, sizeof text);
    CHECK_STR_EQ(text, "TotK is running on PC: close it and retry");
    describe(&busy, text, sizeof text);
    CHECK_STR_EQ(text, "PC is busy: retry in a moment");
    describe(&no_save, text, sizeof text);
    CHECK_STR_EQ(text, "No save on PC yet: start TotK on PC once");
    describe(&write_failed, text, sizeof text);
    CHECK_STR_EQ(text, "PC error: backup failed, PC save not changed: <system message>");
    describe(&wrong_token, text, sizeof text);
    CHECK_STR_EQ(text, "Wrong token: save switch-config.json from the PC launcher");
    describe(&forbidden, text, sizeof text);
    CHECK_STR_EQ(text, "PC refused this connection (HTTP 403): Switch must be in the same private network");
    describe(&not_launcher, text, sizeof text);
    CHECK_STR_EQ(text, "Not a TotK Sync launcher at 192.168.1.10:8765");
    describe(&length_required, text, sizeof text);
    CHECK_STR_EQ(text, "PC error: HTTP 411");
    describe(&too_large, text, sizeof text);
    CHECK_STR_EQ(text, "PC error: HTTP 413");
    describe(&internal, text, sizeof text);
    CHECK_STR_EQ(text, "PC error: HTTP 500");
    describe(&unexpected, text, sizeof text);
    CHECK_STR_EQ(text, "PC error: unexpected answer to the save upload");
}

static void a_connect_failure_says_the_pc_is_not_reachable(void)
{
    char text[TS_LINE_SIZE];
    api_put_result_t put = interpret_text(0, 7, NULL, NULL, 0);
    describe(&put, text, sizeof text);
    CHECK_STR_EQ(text, "PC not reachable: is the launcher open? check firewall and same Wi-Fi");
}

static void a_timeout_before_any_byte_was_sent_says_the_pc_is_not_reachable(void)
{
    char text[TS_LINE_SIZE];
    api_put_result_t put = interpret_text(0, 28, NULL, NULL, 0);
    describe(&put, text, sizeof text);
    CHECK_STR_EQ(text, "PC not reachable: is the launcher open? check firewall and same Wi-Fi");
}

static void a_failure_after_the_body_started_warns_that_the_pc_may_have_the_save(void)
{
    static const int codes[] = { 28, 55, 56, 35 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        char text[TS_LINE_SIZE];
        char expected[TS_LINE_SIZE];
        api_put_result_t put = interpret_text(0, codes[i], NULL, NULL, 1);
        describe(&put, text, sizeof text);
        snprintf(expected, sizeof expected,
                 "Connection lost while sending (curl %d). The PC may have received the save: check the launcher before retrying",
                 codes[i]);
        CHECK_STR_EQ(text, expected);
    }
}

static void a_failure_before_any_byte_was_sent_names_the_curl_code(void)
{
    char text[TS_LINE_SIZE];
    api_put_result_t put = interpret_text(0, 55, NULL, NULL, 0);
    describe(&put, text, sizeof text);
    CHECK_STR_EQ(text, "Network error: curl 55");
}

static void the_token_never_reaches_any_user_text_even_when_the_pc_echoes_it(void)
{
    char body[512];
    char text[TS_LINE_SIZE];
    snprintf(body, sizeof body, "{\"error\":\"invalid_zip\",\"reason\":\"bad token %s here\"}", CONTRACT_TOKEN);
    api_put_result_t invalid_zip = interpret_text(400, 0, LAUNCHER_MARKER_HEADERS, body, 10);
    snprintf(body, sizeof body, "{\"error\":\"write_failed\",\"reason\":\"%s\"}", CONTRACT_TOKEN);
    api_put_result_t write_failed = interpret_text(500, 0, LAUNCHER_MARKER_HEADERS, body, 10);
    describe(&invalid_zip, text, sizeof text);
    CHECK_STR_NOT_CONTAINS(text, CONTRACT_TOKEN);
    CHECK(strstr(text, "PC rejected the save: bad token ") != NULL);
    describe(&write_failed, text, sizeof text);
    CHECK_STR_NOT_CONTAINS(text, CONTRACT_TOKEN);
}

static void every_user_text_is_plain_ascii(void)
{
    static const api_put_kind_t kinds[] = {
        API_PUT_REPLACED, API_PUT_UNREACHABLE, API_PUT_NETWORK_ERROR, API_PUT_WRONG_TOKEN, API_PUT_FORBIDDEN, API_PUT_NOT_LAUNCHER,
        API_PUT_BAD_RESPONSE, API_PUT_INVALID_ZIP, API_PUT_GAME_RUNNING, API_PUT_BUSY, API_PUT_NO_SAVE, API_PUT_WRITE_FAILED,
        API_PUT_HTTP_ERROR,
    };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        char text[TS_LINE_SIZE];
        api_put_result_t put;
        memset(&put, 0, sizeof put);
        put.kind = kinds[i];
        put.http_code = 418;
        put.curl_code = 7;
        snprintf(put.backup, sizeof put.backup, "backup.zip");
        snprintf(put.reason, sizeof put.reason, "some reason");
        describe(&put, text, sizeof text);
        CHECK(test_is_ascii_text(text));
        CHECK(text[0] != '\0');
    }
}

/* ---- force, unchanged and conflict ---- */

static void a_forced_put_request_goes_to_save_with_force_1_and_is_otherwise_the_normal_request(void)
{
    config_t cfg;
    http_request_t normal;
    http_request_t forced;
    body_t zip = { NULL, 1000 };
    test_make_config(&cfg);
    api_build_put_request(&cfg, &zip, &normal);
    api_build_forced_put_request(&cfg, &zip, &forced);
    CHECK_INT_EQ(forced.method, HTTP_PUT);
    CHECK_STR_EQ(forced.url, "http://192.168.1.10:8765/save?force=1");
    CHECK_INT_EQ(forced.header_count, normal.header_count);
    for (int i = 0; i < forced.header_count; i++) {
        CHECK_STR_EQ(forced.headers[i], normal.headers[i]);
    }
    CHECK(forced.body == &zip);
    CHECK_INT_EQ(forced.total_timeout_s, normal.total_timeout_s);
    CHECK_INT_EQ(forced.expect_100_timeout_ms, normal.expect_100_timeout_ms);
    CHECK_INT_EQ(forced.connect_timeout_s, normal.connect_timeout_s);
}

static void a_plain_put_request_never_carries_force(void)
{
    config_t cfg;
    http_request_t req;
    body_t zip = { NULL, 0 };
    test_make_config(&cfg);
    api_build_put_request(&cfg, &zip, &req);
    CHECK(strstr(req.url, "force") == NULL);
}

static void an_unchanged_answer_is_read_from_the_contract_body(void)
{
    api_put_result_t put = interpret_contract("put-unchanged.json", 200, 0, LAUNCHER_MARKER_HEADERS, 5000);
    CHECK_INT_EQ(put.kind, API_PUT_UNCHANGED);
    CHECK_STR_EQ(put.fingerprint, CONTRACT_FINGERPRINT);
    CHECK(put.backup[0] == '\0');
    CHECK_INT_EQ(put.http_code, 200);
}

static void an_unchanged_answer_without_a_valid_fingerprint_is_an_unexpected_answer(void)
{
    static const char *const bodies[] = {
        "{\"result\":\"unchanged\"}",
        "{\"result\":\"unchanged\",\"fingerprint\":\"sha256:abc\"}",
        "{\"result\":\"unchanged\",\"fingerprint\":5}",
        "{\"result\":\"unchanged\",\"fingerprint\":\"sha256:E19ED4007A1FCDCC231A1C9BB8E9F73753913C0DB5328AFC635F94C12F6F4A5E\"}",
        "{\"result\":\"Unchanged\",\"fingerprint\":\"" CONTRACT_FINGERPRINT "\"}",
    };
    for (size_t i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
        api_put_result_t put = interpret_text(200, 0, LAUNCHER_MARKER_HEADERS, bodies[i], 100);
        CHECK_INT_EQ(put.kind, API_PUT_BAD_RESPONSE);
    }
}

static void an_unchanged_answer_without_the_marker_is_not_a_launcher(void)
{
    api_put_result_t put = interpret_contract("put-unchanged.json", 200, 0, NO_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_NOT_LAUNCHER);
}

static void a_conflict_answer_carries_both_dates_without_seconds_and_zone(void)
{
    api_put_result_t put = interpret_contract("error-conflict.json", 409, 0, LAUNCHER_MARKER_HEADERS, 5000);
    CHECK_INT_EQ(put.kind, API_PUT_CONFLICT);
    CHECK_STR_EQ(put.pc_modified, "2026-11-03 14:25");
    CHECK_STR_EQ(put.last_sync_at, "2026-11-03 12:00");
    CHECK_INT_EQ(put.http_code, 409);
}

static void a_conflict_answer_for_a_never_synced_pc_has_an_empty_last_sync(void)
{
    api_put_result_t put = interpret_contract("error-conflict-never-synced.json", 409, 0, LAUNCHER_MARKER_HEADERS, 5000);
    CHECK_INT_EQ(put.kind, API_PUT_CONFLICT);
    CHECK_STR_EQ(put.pc_modified, "2026-11-03 14:25");
    CHECK(put.last_sync_at[0] == '\0');
}

static void a_conflict_answer_without_a_last_sync_key_counts_as_never_synced(void)
{
    api_put_result_t put = interpret_text(409, 0, LAUNCHER_MARKER_HEADERS,
                                          "{\"error\":\"conflict\",\"pc_modified\":\"2026-11-03T14:25:01+03:00\"}", 100);
    CHECK_INT_EQ(put.kind, API_PUT_CONFLICT);
    CHECK(put.last_sync_at[0] == '\0');
}

static void a_conflict_refusal_that_arrived_before_the_connection_broke_is_still_a_conflict(void)
{
    api_put_result_t put = interpret_contract("error-conflict.json", 409, 56, LAUNCHER_MARKER_HEADERS, 300);
    CHECK_INT_EQ(put.kind, API_PUT_CONFLICT);
}

static void a_409_that_is_not_a_well_formed_conflict_document_is_a_plain_http_error(void)
{
    static const char *const bodies[] = {
        "",
        "not json",
        "{}",
        "[]",
        "{\"error\":\"conflict\"}",
        "{\"error\":\"busy\",\"pc_modified\":\"2026-11-03T14:25:01+03:00\",\"last_sync_at\":null}",
        "{\"error\":\"conflict\",\"pc_modified\":\"yesterday\",\"last_sync_at\":null}",
        "{\"error\":\"conflict\",\"pc_modified\":5,\"last_sync_at\":null}",
        "{\"error\":\"conflict\",\"pc_modified\":\"2026-11-03T14:25:01+03:00\",\"last_sync_at\":\"whenever\"}",
        "{\"error\":\"conflict\",\"pc_modified\":\"2026-11-03T14:25:01+03:00\",\"last_sync_at\":7}",
    };
    for (size_t i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
        api_put_result_t put = interpret_text(409, 0, LAUNCHER_MARKER_HEADERS, bodies[i], 100);
        CHECK_INT_EQ(put.kind, API_PUT_HTTP_ERROR);
        CHECK_INT_EQ(put.http_code, 409);
    }
}

static void a_409_without_any_body_is_a_plain_http_error(void)
{
    api_put_result_t put = interpret_text(409, 0, LAUNCHER_MARKER_HEADERS, NULL, 100);
    CHECK_INT_EQ(put.kind, API_PUT_HTTP_ERROR);
}

static void a_409_without_the_marker_is_not_a_launcher(void)
{
    api_put_result_t put = interpret_contract("error-conflict.json", 409, 0, NO_MARKER_HEADERS, 0);
    CHECK_INT_EQ(put.kind, API_PUT_NOT_LAUNCHER);
}

static void the_unchanged_answer_has_the_exact_text_from_the_spec(void)
{
    char text[TS_LINE_SIZE];
    api_put_result_t put = interpret_contract("put-unchanged.json", 200, 0, LAUNCHER_MARKER_HEADERS, 5000);
    describe(&put, text, sizeof text);
    CHECK_STR_EQ(text, "PC already has this save");
}

static void a_malformed_409_reads_as_pc_error_http_409(void)
{
    char text[TS_LINE_SIZE];
    api_put_result_t put = interpret_text(409, 0, LAUNCHER_MARKER_HEADERS, "{}", 100);
    describe(&put, text, sizeof text);
    CHECK_STR_EQ(text, "PC error: HTTP 409");
}

static void the_overwrite_prompt_has_the_exact_text_from_the_spec_with_both_dates(void)
{
    char prompt[TS_LINE_SIZE];
    api_put_result_t put = interpret_contract("error-conflict.json", 409, 0, LAUNCHER_MARKER_HEADERS, 0);
    send_format_conflict_prompt(&put, prompt, sizeof prompt);
    CHECK_STR_EQ(prompt, "PC save changed since last sync (PC: 2026-11-03 14:25, last sync: 2026-11-03 12:00). "
                         "Overwrite PC save? A - yes, B - no");
}

static void the_overwrite_prompt_says_never_when_the_pc_has_no_last_sync(void)
{
    char prompt[TS_LINE_SIZE];
    api_put_result_t put = interpret_contract("error-conflict-never-synced.json", 409, 0, LAUNCHER_MARKER_HEADERS, 0);
    send_format_conflict_prompt(&put, prompt, sizeof prompt);
    CHECK_STR_EQ(prompt, "PC save changed since last sync (PC: 2026-11-03 14:25, last sync: never). "
                         "Overwrite PC save? A - yes, B - no");
}

static void the_overwrite_prompt_is_plain_ascii_and_stays_terminated_in_a_small_buffer(void)
{
    char prompt[TS_LINE_SIZE];
    char small[16];
    api_put_result_t put = interpret_contract("error-conflict.json", 409, 0, LAUNCHER_MARKER_HEADERS, 0);
    send_format_conflict_prompt(&put, prompt, sizeof prompt);
    CHECK(test_is_ascii_text(prompt));
    send_format_conflict_prompt(&put, small, sizeof small);
    CHECK_INT_EQ(strlen(small), sizeof small - 1);
}

void run_api_put_tests(void)
{
    RUN_TEST(put_request_is_a_put_to_http_host_port_save);
    RUN_TEST(put_request_follows_the_configured_host_and_port);
    RUN_TEST(put_request_has_exactly_the_authorization_content_type_and_expect_headers);
    RUN_TEST(the_token_appears_only_in_the_authorization_header);
    RUN_TEST(put_request_waits_three_seconds_for_100_continue);
    RUN_TEST(put_request_hands_over_the_zip_so_the_length_is_known);
    RUN_TEST(put_request_has_a_5_second_connect_timeout_and_keeps_the_json_response_limit);
    RUN_TEST(the_total_timeout_is_30_seconds_plus_the_time_at_64_kib_per_second);
    RUN_TEST(the_total_timeout_never_exceeds_900_seconds);
    RUN_TEST(the_request_total_timeout_follows_the_zip_size);
    RUN_TEST(a_replaced_answer_is_read_from_the_contract_body);
    RUN_TEST(a_200_body_that_is_not_a_replaced_result_is_an_unexpected_answer);
    RUN_TEST(a_200_without_a_body_at_all_is_an_unexpected_answer);
    RUN_TEST(a_fingerprint_that_is_not_sha256_and_64_lowercase_hex_is_an_unexpected_answer);
    RUN_TEST(a_backup_name_that_could_point_outside_the_backup_folder_is_an_unexpected_answer);
    RUN_TEST(a_backup_name_of_128_characters_is_too_long_and_127_is_fine);
    RUN_TEST(a_wrong_token_is_a_401_from_a_launcher);
    RUN_TEST(a_403_from_a_launcher_is_forbidden);
    RUN_TEST(any_answer_without_the_marker_is_not_a_launcher);
    RUN_TEST(the_marker_must_have_the_value_1);
    RUN_TEST(an_invalid_zip_answer_carries_the_reason_of_the_pc);
    RUN_TEST(an_invalid_zip_reason_is_made_printable_and_cut_to_fit);
    RUN_TEST(an_invalid_zip_answer_without_a_reason_says_no_details);
    RUN_TEST(a_400_that_is_not_an_invalid_zip_document_is_a_plain_http_error);
    RUN_TEST(a_423_means_the_game_runs_on_the_pc);
    RUN_TEST(a_503_means_the_pc_is_busy);
    RUN_TEST(a_404_no_save_document_means_the_pc_has_no_save);
    RUN_TEST(a_404_for_an_unknown_path_is_a_plain_http_error);
    RUN_TEST(a_500_write_failed_carries_the_reason_that_names_the_backup);
    RUN_TEST(a_500_internal_is_a_plain_http_error);
    RUN_TEST(other_codes_from_a_launcher_are_plain_http_errors);
    RUN_TEST(the_unreachable_curl_codes_are_6_7_and_28);
    RUN_TEST(other_curl_failures_without_an_answer_are_network_errors);
    RUN_TEST(only_curl_55_and_56_after_a_refusal_code_keep_the_refusal);
    RUN_TEST(a_refusal_that_arrived_before_the_connection_broke_counts_as_the_refusal);
    RUN_TEST(a_refusal_from_a_foreign_server_before_the_connection_broke_is_not_a_launcher);
    RUN_TEST(a_broken_connection_after_a_success_code_is_a_network_error_not_a_success);
    RUN_TEST(each_answer_has_the_exact_text_from_the_spec);
    RUN_TEST(a_connect_failure_says_the_pc_is_not_reachable);
    RUN_TEST(a_timeout_before_any_byte_was_sent_says_the_pc_is_not_reachable);
    RUN_TEST(a_failure_after_the_body_started_warns_that_the_pc_may_have_the_save);
    RUN_TEST(a_failure_before_any_byte_was_sent_names_the_curl_code);
    RUN_TEST(the_token_never_reaches_any_user_text_even_when_the_pc_echoes_it);
    RUN_TEST(every_user_text_is_plain_ascii);
    RUN_TEST(a_forced_put_request_goes_to_save_with_force_1_and_is_otherwise_the_normal_request);
    RUN_TEST(a_plain_put_request_never_carries_force);
    RUN_TEST(an_unchanged_answer_is_read_from_the_contract_body);
    RUN_TEST(an_unchanged_answer_without_a_valid_fingerprint_is_an_unexpected_answer);
    RUN_TEST(an_unchanged_answer_without_the_marker_is_not_a_launcher);
    RUN_TEST(a_conflict_answer_carries_both_dates_without_seconds_and_zone);
    RUN_TEST(a_conflict_answer_for_a_never_synced_pc_has_an_empty_last_sync);
    RUN_TEST(a_conflict_answer_without_a_last_sync_key_counts_as_never_synced);
    RUN_TEST(a_conflict_refusal_that_arrived_before_the_connection_broke_is_still_a_conflict);
    RUN_TEST(a_409_that_is_not_a_well_formed_conflict_document_is_a_plain_http_error);
    RUN_TEST(a_409_without_any_body_is_a_plain_http_error);
    RUN_TEST(a_409_without_the_marker_is_not_a_launcher);
    RUN_TEST(the_unchanged_answer_has_the_exact_text_from_the_spec);
    RUN_TEST(a_malformed_409_reads_as_pc_error_http_409);
    RUN_TEST(the_overwrite_prompt_has_the_exact_text_from_the_spec_with_both_dates);
    RUN_TEST(the_overwrite_prompt_says_never_when_the_pc_has_no_last_sync);
    RUN_TEST(the_overwrite_prompt_is_plain_ascii_and_stays_terminated_in_a_small_buffer);
}
