#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "body.h"
#include "config.h"
#include "fs_compat.h"
#include "log.h"
#include "outcome.h"
#include "paths.h"
#include "session.h"
#include "test.h"
#include "test_fs.h"
#include "ts_limits.h"

/* The libnx console font cannot render non-ASCII, so every string shown on screen or written to the log must be 7-bit. */

#ifndef TS_CORE_DIR
#error "TS_CORE_DIR must point to source/core (set in tests/Makefile)"
#endif

#define MAX_COLLECTED_LINES 32
#define LINE_CAPACITY 1024
#define LOG_CAPACITY 16384

typedef struct {
    char lines[MAX_COLLECTED_LINES][LINE_CAPACITY];
    int line_count;
    upload_outcome_t outcome;
} collector_t;

static bool is_ascii(const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        if (*p >= 0x80) {
            return false;
        }
    }
    return true;
}

static void collector_perform(void *ctx, const upload_request_t *req, const body_t *body, upload_outcome_t *out)
{
    (void)req;
    (void)body;
    *out = ((collector_t *)ctx)->outcome;
}

static void collector_sleep(void *ctx, int seconds)
{
    (void)ctx;
    (void)seconds;
}

static void collector_now(void *ctx, struct tm *out)
{
    (void)ctx;
    memset(out, 0, sizeof *out);
    out->tm_year = 2026 - 1900;
    out->tm_mon = 10;
    out->tm_mday = 3;
}

static void collector_show_line(void *ctx, const char *line)
{
    collector_t *c = ctx;
    if (c->line_count < MAX_COLLECTED_LINES) {
        snprintf(c->lines[c->line_count++], LINE_CAPACITY, "%s", line);
    }
}

static void check_all_lines_ascii(const collector_t *c)
{
    CHECK(c->line_count > 0);
    for (int i = 0; i < c->line_count; i++) {
        if (!is_ascii(c->lines[i])) {
            printf("non-ASCII line: %s\n", c->lines[i]);
        }
        CHECK(is_ascii(c->lines[i]));
    }
}

static config_t sample_config(void)
{
    config_t cfg = {0};
    strcpy(cfg.origin, "https://webdav.yandex.com");
    strcpy(cfg.basepath, "JKSV/The Legend of Zelda Tears of the Kingdom");
    strcpy(cfg.username, "LOGIN");
    strcpy(cfg.password, "TopSecretAppPassword");
    return cfg;
}

static upload_outcome_t http_result(long http_code)
{
    upload_outcome_t o = {0};
    o.http_code = http_code;
    o.bytes_sent = 5;
    strcpy(o.curl_error, "No error");
    return o;
}

static upload_outcome_t curl_failure(int curl_code)
{
    upload_outcome_t o = {0};
    o.curl_code = curl_code;
    strcpy(o.curl_error, "boom");
    return o;
}

static void run_session_with_config(collector_t *c, const config_t *cfg, const char *root, const upload_outcome_t *outcome, log_sink_t *log)
{
    session_inputs_t in = {cfg, root, NULL, log};
    upload_port_t port = {c, collector_perform, collector_sleep, collector_now, collector_show_line};
    memset(c, 0, sizeof *c);
    c->outcome = *outcome;
    session_upload_test_file(&in, &port);
}

static void run_session(collector_t *c, const char *root, const upload_outcome_t *outcome, log_sink_t *log)
{
    config_t cfg = sample_config();
    run_session_with_config(c, &cfg, root, outcome, log);
}

static void make_root_with_test_zip(char *root, size_t size)
{
    CHECK(test_fs_make_root(root, size));
    CHECK(test_fs_write_file(root, TS_REL_TEST_ZIP, "PK123", 5));
}

/* ---- text.h source guard ---- */

static void text_header_contains_only_ascii_bytes(void)
{
    FILE *file = fopen(TS_CORE_DIR "/text.h", "rb");
    long long non_ascii_bytes = 0;
    long long total_bytes = 0;
    int byte;
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    while ((byte = fgetc(file)) != EOF) {
        total_bytes++;
        if (byte >= 0x80) {
            non_ascii_bytes++;
        }
    }
    fclose(file);
    CHECK(total_bytes > 0);
    CHECK_INT_EQ(non_ascii_bytes, 0);
}

/* ---- session output ---- */

static void successful_session_shows_only_ascii_lines(void)
{
    char root[128];
    collector_t c;
    upload_outcome_t outcome = http_result(201);
    make_root_with_test_zip(root, sizeof root);
    run_session(&c, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_401_shows_only_ascii_lines(void)
{
    char root[128];
    collector_t c;
    upload_outcome_t outcome = http_result(401);
    make_root_with_test_zip(root, sizeof root);
    run_session(&c, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_connection_reset_retry_shows_only_ascii_lines(void)
{
    char root[128];
    collector_t c;
    upload_outcome_t outcome = curl_failure(56);
    make_root_with_test_zip(root, sizeof root);
    run_session(&c, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_certificate_error_shows_only_ascii_lines(void)
{
    char root[128];
    collector_t c;
    upload_outcome_t outcome = curl_failure(60);
    make_root_with_test_zip(root, sizeof root);
    run_session(&c, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_missing_test_zip_shows_only_ascii_lines(void)
{
    char root[128];
    collector_t c;
    upload_outcome_t outcome = http_result(201);
    CHECK(test_fs_make_root(root, sizeof root));
    run_session(&c, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_empty_test_zip_shows_only_ascii_lines(void)
{
    char root[128];
    collector_t c;
    upload_outcome_t outcome = http_result(201);
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, TS_REL_TEST_ZIP, "", 0));
    run_session(&c, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_oversized_test_zip_shows_only_ascii_lines(void)
{
    char root[128];
    char path[256];
    collector_t c;
    upload_outcome_t outcome = http_result(201);
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, TS_REL_TEST_ZIP, "x", 1));
    paths_fs(root, TS_REL_TEST_ZIP, path, sizeof path);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    fseek(file, (long)TS_MAX_BODY_BYTES, SEEK_SET);
    fputc('x', file);
    fclose(file);
    run_session(&c, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_unbuildable_request_shows_only_ascii_lines(void)
{
    char root[128];
    collector_t c;
    upload_outcome_t outcome = http_result(201);
    config_t cfg = sample_config();
    memset(cfg.basepath, ' ', 500);
    cfg.basepath[500] = '\0';
    make_root_with_test_zip(root, sizeof root);
    run_session_with_config(&c, &cfg, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_ca_pem_present_shows_only_ascii_lines(void)
{
    char root[128];
    collector_t c;
    upload_outcome_t outcome = http_result(201);
    make_root_with_test_zip(root, sizeof root);
    CHECK(test_fs_write_file(root, TS_REL_CA_PEM, "x", 1));
    run_session(&c, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_ca_pem_absent_shows_only_ascii_lines(void)
{
    char root[128];
    collector_t c;
    upload_outcome_t outcome = http_result(201);
    make_root_with_test_zip(root, sizeof root);
    run_session(&c, root, &outcome, NULL);
    check_all_lines_ascii(&c);
}

static void session_with_unavailable_log_shows_only_ascii_lines(void)
{
    char root[128];
    char probe[128];
    char log_root[256];
    collector_t c;
    log_sink_t log;
    upload_outcome_t outcome = http_result(201);
    make_root_with_test_zip(root, sizeof root);
    CHECK(test_fs_make_root(probe, sizeof probe));
    CHECK(test_fs_write_file(probe, "plain-file", "x", 1));
    snprintf(log_root, sizeof log_root, "%s/plain-file", probe);
    log_sink_init(&log, log_root);
    run_session(&c, root, &outcome, &log);
    CHECK(strstr(c.lines[c.line_count - 1], "Log unavailable") != NULL);
    check_all_lines_ascii(&c);
}

/* ---- log.txt content ---- */

static void log_file_written_during_session_contains_only_ascii(void)
{
    char root[128];
    char text[LOG_CAPACITY];
    collector_t c;
    log_sink_t log;
    upload_outcome_t outcome = curl_failure(56);
    make_root_with_test_zip(root, sizeof root);
    log_sink_init(&log, root);
    run_session(&c, root, &outcome, &log);
    FILE *file = fopen(log.file_path, "rb");
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    size_t count = fread(text, 1, sizeof text - 1, file);
    fclose(file);
    text[count] = '\0';
    CHECK(count > 0);
    CHECK(is_ascii(text));
}

/* ---- message builders ---- */

static void outcome_messages_are_ascii_for_every_outcome_kind(void)
{
    upload_outcome_t outcomes[] = {
        http_result(201), http_result(401), http_result(409), http_result(507), http_result(500),
        curl_failure(6), curl_failure(7), curl_failure(28), curl_failure(56),
        curl_failure(35), curl_failure(60), curl_failure(23),
    };
    for (size_t i = 0; i < sizeof outcomes / sizeof outcomes[0]; i++) {
        char message[512];
        outcome_message(&outcomes[i], message, sizeof message);
        CHECK(message[0] != '\0');
        CHECK(is_ascii(message));
    }
}

static void tls_outcome_message_includes_curl_code(void)
{
    char message[512];
    upload_outcome_t outcome = curl_failure(60);
    outcome_message(&outcome, message, sizeof message);
    CHECK(strstr(message, "60") != NULL);
}

static void body_status_messages_are_ascii_for_every_failure(void)
{
    body_status_t statuses[] = {BODY_ERR_NOT_FOUND, BODY_ERR_EMPTY, BODY_ERR_TOO_LARGE, BODY_ERR_READ, BODY_ERR_NO_MEMORY};
    for (size_t i = 0; i < sizeof statuses / sizeof statuses[0]; i++) {
        char message[512];
        body_status_message(statuses[i], "sd:/switch/totk-sync/test.zip", 67108865, TS_MAX_BODY_BYTES, message, sizeof message);
        CHECK(message[0] != '\0');
        CHECK(is_ascii(message));
    }
}

static void check_parse_error_message_is_ascii(const char *json, config_status_t expected)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(config_parse(json, CONFIG_SOURCE_TOTK_SYNC, "sd:/config/totk-sync/config.json", &cfg, &err), expected);
    CHECK(err.message[0] != '\0');
    CHECK(is_ascii(err.message));
}

static void config_parse_error_messages_are_ascii(void)
{
    char too_long[600];
    char json[800];
    memset(too_long, 'a', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = '\0';
    snprintf(json, sizeof json, "{\"origin\":\"https://h\",\"basepath\":\"b\",\"username\":\"%s\",\"password\":\"p\"}", too_long);

    check_parse_error_message_is_ascii("{not json", CONFIG_ERR_PARSE);
    check_parse_error_message_is_ascii("{\"origin\":\"https://h\"}", CONFIG_ERR_FIELD);
    check_parse_error_message_is_ascii(json, CONFIG_ERR_FIELD_TOO_LONG);
    check_parse_error_message_is_ascii("{\"origin\":\"http://h\",\"basepath\":\"b\",\"username\":\"u\",\"password\":\"p\"}", CONFIG_ERR_INSECURE_ORIGIN);
}

static void config_jksv_basepath_too_long_message_is_ascii(void)
{
    config_t cfg;
    config_error_t err;
    char base[512];
    char json[800];
    memset(base, 'a', 480);
    base[480] = '\0';
    snprintf(json, sizeof json, "{\"origin\":\"https://h\",\"basepath\":\"%s\",\"username\":\"u\",\"password\":\"p\"}", base);
    CHECK_INT_EQ(config_parse(json, CONFIG_SOURCE_JKSV, "sd:/config/JKSV/webdav.json", &cfg, &err), CONFIG_ERR_FIELD_TOO_LONG);
    CHECK(is_ascii(err.message));
}

static void config_load_missing_file_message_is_ascii(void)
{
    char root[128];
    config_t cfg;
    config_error_t err;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_NOT_FOUND);
    CHECK(err.message[0] != '\0');
    CHECK(is_ascii(err.message));
}

static void config_load_read_error_message_is_ascii(void)
{
    char root[128];
    char path[512];
    config_t cfg;
    config_error_t err;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, TS_REL_CONFIG ".keep", "x", 1));
    snprintf(path, sizeof path, "%s/%s", root, TS_REL_CONFIG);
    CHECK_INT_EQ(ts_mkdir(path), 0);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_READ);
    CHECK(is_ascii(err.message));
}

void run_ui_text_tests(void)
{
    RUN_TEST(text_header_contains_only_ascii_bytes);
    RUN_TEST(successful_session_shows_only_ascii_lines);
    RUN_TEST(session_with_401_shows_only_ascii_lines);
    RUN_TEST(session_with_connection_reset_retry_shows_only_ascii_lines);
    RUN_TEST(session_with_certificate_error_shows_only_ascii_lines);
    RUN_TEST(session_with_missing_test_zip_shows_only_ascii_lines);
    RUN_TEST(session_with_empty_test_zip_shows_only_ascii_lines);
    RUN_TEST(session_with_oversized_test_zip_shows_only_ascii_lines);
    RUN_TEST(session_with_unbuildable_request_shows_only_ascii_lines);
    RUN_TEST(session_with_ca_pem_present_shows_only_ascii_lines);
    RUN_TEST(session_with_ca_pem_absent_shows_only_ascii_lines);
    RUN_TEST(session_with_unavailable_log_shows_only_ascii_lines);
    RUN_TEST(log_file_written_during_session_contains_only_ascii);
    RUN_TEST(outcome_messages_are_ascii_for_every_outcome_kind);
    RUN_TEST(tls_outcome_message_includes_curl_code);
    RUN_TEST(body_status_messages_are_ascii_for_every_failure);
    RUN_TEST(config_parse_error_messages_are_ascii);
    RUN_TEST(config_jksv_basepath_too_long_message_is_ascii);
    RUN_TEST(config_load_missing_file_message_is_ascii);
    RUN_TEST(config_load_read_error_message_is_ascii);
}
