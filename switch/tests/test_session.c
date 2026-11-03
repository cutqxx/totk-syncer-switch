#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "paths.h"
#include "session.h"
#include "test.h"
#include "test_fs.h"
#include "ts_limits.h"

#define MAX_LINES 16
#define MAX_ATTEMPTS_RECORDED 4
#define LINE_CAPACITY 512
#define MAX_CLOCK_READINGS 4

typedef struct {
    struct tm clock[MAX_CLOCK_READINGS];
    int clock_reads;
    upload_outcome_t outcome;
    int perform_calls;
    upload_request_t last_request;
    upload_outcome_t script[MAX_ATTEMPTS_RECORDED];
    int script_length;
    upload_request_t requests[MAX_ATTEMPTS_RECORDED];
    char line_before_perform[MAX_ATTEMPTS_RECORDED][LINE_CAPACITY];
    int sleep_calls;
    int sleep_arguments[MAX_ATTEMPTS_RECORDED];
    int64_t last_body_size;
    unsigned char last_body_first_byte;
    char lines[MAX_LINES][LINE_CAPACITY];
    int line_count;
} fake_t;

static void fake_perform(void *ctx, const upload_request_t *req, const body_t *body, upload_outcome_t *out)
{
    fake_t *fake = ctx;
    int index = fake->perform_calls;
    if (index < MAX_ATTEMPTS_RECORDED) {
        fake->requests[index] = *req;
        if (fake->line_count > 0) {
            strcpy(fake->line_before_perform[index], fake->lines[fake->line_count - 1]);
        }
    }
    fake->perform_calls++;
    fake->last_request = *req;
    fake->last_body_size = body->size;
    fake->last_body_first_byte = body->data[0];
    *out = index < fake->script_length ? fake->script[index] : fake->outcome;
}

static void fake_sleep_seconds(void *ctx, int seconds)
{
    fake_t *fake = ctx;
    if (fake->sleep_calls < MAX_ATTEMPTS_RECORDED) {
        fake->sleep_arguments[fake->sleep_calls] = seconds;
    }
    fake->sleep_calls++;
}

static void fake_now_local(void *ctx, struct tm *out)
{
    fake_t *fake = ctx;
    *out = fake->clock[fake->clock_reads < MAX_CLOCK_READINGS ? fake->clock_reads : MAX_CLOCK_READINGS - 1];
    fake->clock_reads++;
}

static void fake_show_line(void *ctx, const char *line)
{
    fake_t *fake = ctx;
    if (fake->line_count < MAX_LINES) {
        snprintf(fake->lines[fake->line_count], LINE_CAPACITY, "%s", line);
        fake->line_count++;
    }
}

static struct tm clock_at(int second)
{
    struct tm t = {0};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 10;
    t.tm_mday = 3;
    t.tm_hour = 14;
    t.tm_min = 25;
    t.tm_sec = second;
    return t;
}

static void init_fake(fake_t *fake, long http_code)
{
    memset(fake, 0, sizeof *fake);
    for (int i = 0; i < MAX_CLOCK_READINGS; i++) {
        fake->clock[i] = clock_at(i + 1);
    }
    fake->outcome.http_code = http_code;
    fake->outcome.bytes_sent = 5;
    strcpy(fake->outcome.curl_error, "No error");
}

static upload_port_t port_for(fake_t *fake)
{
    upload_port_t port = {fake, fake_perform, fake_sleep_seconds,fake_now_local, fake_show_line};
    return port;
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

static void make_sd_with_test_zip(char *root, size_t root_size)
{
    CHECK(test_fs_make_root(root, root_size));
    CHECK(test_fs_write_file(root, TS_REL_TEST_ZIP, "PK123", 5));
}

static outcome_kind_t upload_once(fake_t *fake, const char *root, const config_t *cfg)
{
    session_inputs_t in = {cfg, root, NULL, NULL};
    upload_port_t port = port_for(fake);
    return session_upload_test_file(&in, &port);
}

static void upload_with_201_reports_success(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_SUCCESS);
}

static void upload_with_204_reports_success(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 204);
    make_sd_with_test_zip(root, sizeof root);
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_SUCCESS);
}

static void upload_with_non_2xx_does_not_report_success(void)
{
    long codes[] = {199, 300, 401, 409, 500, 507};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        char root[128];
        fake_t fake;
        config_t cfg = sample_config();
        init_fake(&fake, codes[i]);
        make_sd_with_test_zip(root, sizeof root);
        CHECK(upload_once(&fake, root, &cfg) != OUTCOME_SUCCESS);
    }
}

static void upload_with_curl_error_does_not_report_success_even_with_2xx_code(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    fake.outcome.curl_code = 23;
    make_sd_with_test_zip(root, sizeof root);
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_CURL_OTHER);
}

static void upload_sends_one_request_with_file_content_and_size(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&fake, root, &cfg);
    CHECK_INT_EQ(fake.perform_calls, 1);
    CHECK_INT_EQ(fake.last_body_size, 5);
    CHECK_INT_EQ(fake.last_body_first_byte, 'P');
    CHECK_INT_EQ(fake.last_request.content_length, 5);
}

static void upload_names_remote_file_after_console_clock(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&fake, root, &cfg);
    CHECK_STR_EQ(fake.last_request.url,
                 "https://webdav.yandex.com/JKSV/The%20Legend%20of%20Zelda%20Tears%20of%20the%20Kingdom/spike_20261103-142501.zip");
}

static void two_uploads_with_different_clock_readings_target_different_urls(void)
{
    char root[128];
    fake_t first, second;
    config_t cfg = sample_config();
    init_fake(&first, 201);
    init_fake(&second, 201);
    second.clock[0] = clock_at(30);
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&first, root, &cfg);
    upload_once(&second, root, &cfg);
    CHECK(strcmp(first.last_request.url, second.last_request.url) != 0);
    CHECK(strstr(second.last_request.url, "spike_20261103-142530.zip") != NULL);
}

static void upload_uses_system_certificates_when_ca_pem_absent(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&fake, root, &cfg);
    CHECK_STR_EQ(fake.last_request.ca_file, "");
}

static void upload_passes_ca_pem_path_when_present_on_sd(void)
{
    char root[128];
    char expected[256];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    CHECK(test_fs_write_file(root, TS_REL_CA_PEM, "-----BEGIN CERTIFICATE-----", 27));
    upload_once(&fake, root, &cfg);
    paths_fs(root, TS_REL_CA_PEM, expected, sizeof expected);
    CHECK_STR_EQ(fake.last_request.ca_file, expected);
}

static void upload_never_shows_password_on_any_output_line(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 401);
    make_sd_with_test_zip(root, sizeof root);
    CHECK(test_fs_write_file(root, TS_REL_CA_PEM, "x", 1));
    upload_once(&fake, root, &cfg);
    CHECK(fake.line_count > 0);
    for (int i = 0; i < fake.line_count; i++) {
        CHECK_STR_NOT_CONTAINS(fake.lines[i], cfg.password);
    }
}

static void upload_result_line_shows_http_curl_time_bytes_and_verdict(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    fake.outcome.seconds = 12.4;
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&fake, root, &cfg);
    const char *line = fake.lines[fake.line_count - 2];
    CHECK(strstr(line, "HTTP 201") != NULL);
    CHECK(strstr(line, "curl 0 (No error)") != NULL);
    CHECK(strstr(line, "12.4 s") != NULL);
    CHECK(strstr(line, "5/5 B") != NULL);
    CHECK(strstr(line, "SUCCESS") != NULL);
}

static void upload_failure_line_shows_error_verdict_not_success(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 500);
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&fake, root, &cfg);
    const char *line = fake.lines[fake.line_count - 2];
    CHECK(strstr(line, "ERROR") != NULL);
    CHECK_STR_NOT_CONTAINS(line, "SUCCESS");
}

static void missing_test_zip_fails_preflight_without_network_request(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_PREFLIGHT_FAILED);
    CHECK_INT_EQ(fake.perform_calls, 0);
    CHECK(strstr(fake.lines[0], "sd:/switch/totk-sync/test.zip") != NULL);
}

static upload_outcome_t curl_failure(int curl_code)
{
    upload_outcome_t o = {0};
    o.curl_code = curl_code;
    strcpy(o.curl_error, "boom");
    return o;
}

static upload_outcome_t http_result(long http_code)
{
    upload_outcome_t o = {0};
    o.http_code = http_code;
    o.bytes_sent = 5;
    strcpy(o.curl_error, "No error");
    return o;
}

static bool any_line_contains(const fake_t *fake, const char *needle)
{
    for (int i = 0; i < fake->line_count; i++) {
        if (strstr(fake->lines[i], needle) != NULL) {
            return true;
        }
    }
    return false;
}

static void script_reset_then_success(fake_t *fake)
{
    fake->script[0] = curl_failure(56);
    fake->script[1] = http_result(201);
    fake->script_length = 2;
}

static void upload_retries_once_after_connection_reset_and_succeeds(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    script_reset_then_success(&fake);
    make_sd_with_test_zip(root, sizeof root);
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_SUCCESS);
    CHECK_INT_EQ(fake.perform_calls, 2);
    CHECK_INT_EQ(fake.sleep_calls, 1);
    CHECK_INT_EQ(fake.sleep_arguments[0], 3);
}

static void retry_uses_a_new_remote_name_per_attempt(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    script_reset_then_success(&fake);
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&fake, root, &cfg);
    CHECK(strcmp(fake.requests[0].url, fake.requests[1].url) != 0);
    CHECK(strstr(fake.requests[0].url, "spike_20261103-142501.zip") != NULL);
    CHECK(strstr(fake.requests[1].url, "spike_20261103-142502.zip") != NULL);
}

static void upload_fails_after_two_connection_resets_with_clear_message(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 0);
    fake.outcome = curl_failure(56);
    make_sd_with_test_zip(root, sizeof root);
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_NET_RECV);
    CHECK_INT_EQ(fake.perform_calls, 2);
    CHECK_INT_EQ(fake.sleep_calls, 1);
    CHECK(any_line_contains(&fake, "Connection dropped during transfer (56)"));
}

static void upload_retries_on_timeout_resolve_and_connect_errors(void)
{
    int codes[] = {6, 7, 28};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        char root[128];
        fake_t fake;
        config_t cfg = sample_config();
        init_fake(&fake, 0);
        fake.outcome = curl_failure(codes[i]);
        make_sd_with_test_zip(root, sizeof root);
        upload_once(&fake, root, &cfg);
        CHECK_INT_EQ(fake.perform_calls, 2);
        CHECK_INT_EQ(fake.sleep_calls, 1);
    }
}

static void upload_does_not_retry_on_tls_errors(void)
{
    int codes[] = {35, 60};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        char root[128];
        fake_t fake;
        config_t cfg = sample_config();
        init_fake(&fake, 0);
        fake.outcome = curl_failure(codes[i]);
        make_sd_with_test_zip(root, sizeof root);
        CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_TLS);
        CHECK_INT_EQ(fake.perform_calls, 1);
        CHECK_INT_EQ(fake.sleep_calls, 0);
    }
}

static void upload_does_not_retry_on_http_errors(void)
{
    long codes[] = {401, 409, 500, 507};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        char root[128];
        fake_t fake;
        config_t cfg = sample_config();
        init_fake(&fake, codes[i]);
        make_sd_with_test_zip(root, sizeof root);
        upload_once(&fake, root, &cfg);
        CHECK_INT_EQ(fake.perform_calls, 1);
        CHECK_INT_EQ(fake.sleep_calls, 0);
    }
}

static void upload_with_401_shows_wrong_credentials_message(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 401);
    make_sd_with_test_zip(root, sizeof root);
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_AUTH);
    CHECK(any_line_contains(&fake, "Wrong login/password (401)"));
}

static void tls_verification_stays_enabled_on_every_attempt(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 0);
    fake.outcome = curl_failure(56);
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&fake, root, &cfg);
    CHECK_INT_EQ(fake.perform_calls, 2);
    for (int i = 0; i < fake.perform_calls; i++) {
        CHECK(fake.requests[i].verify_peer);
        CHECK(fake.requests[i].verify_host);
    }
}

static void each_attempt_shows_uploading_line_before_perform_and_its_own_result_line(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    script_reset_then_success(&fake);
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&fake, root, &cfg);
    CHECK_STR_EQ(fake.line_before_perform[0], "Uploading... (attempt 1/2)");
    CHECK_STR_EQ(fake.line_before_perform[1], "Uploading... (attempt 2/2)");
    CHECK(any_line_contains(&fake, "attempt 1/2"));
    CHECK(any_line_contains(&fake, "attempt 2/2"));
    CHECK(any_line_contains(&fake, "Retrying in 3 s..."));
}

static void upload_with_empty_test_zip_fails_preflight_with_message(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, TS_REL_TEST_ZIP, "", 0));
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_PREFLIGHT_FAILED);
    CHECK_INT_EQ(fake.perform_calls, 0);
    CHECK_STR_EQ(fake.lines[0], "File is empty (0 bytes): sd:/switch/totk-sync/test.zip");
}

static void upload_with_missing_test_zip_shows_not_found_message(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    CHECK(test_fs_make_root(root, sizeof root));
    upload_once(&fake, root, &cfg);
    CHECK_STR_EQ(fake.lines[0], "File not found: sd:/switch/totk-sync/test.zip");
}

static void upload_with_oversized_test_zip_fails_preflight_showing_size(void)
{
    char root[128];
    char path[256];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
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
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_PREFLIGHT_FAILED);
    CHECK_INT_EQ(fake.perform_calls, 0);
    CHECK_STR_EQ(fake.lines[0], "File too large (67108865 bytes, maximum 67108864 bytes): sd:/switch/totk-sync/test.zip");
}

static void upload_with_unbuildable_request_shows_error_and_performs_nothing(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    memset(cfg.basepath, ' ', 500);
    cfg.basepath[500] = '\0';
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_PREFLIGHT_FAILED);
    CHECK_INT_EQ(fake.perform_calls, 0);
    CHECK(any_line_contains(&fake, "Cannot build request"));
}

static void upload_announces_ca_pem_when_present(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    CHECK(test_fs_write_file(root, TS_REL_CA_PEM, "x", 1));
    upload_once(&fake, root, &cfg);
    CHECK_STR_EQ(fake.lines[0], "CA: using sd:/switch/totk-sync/ca.pem");
}

static void upload_announces_system_certificates_when_ca_pem_absent(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    upload_once(&fake, root, &cfg);
    CHECK_STR_EQ(fake.lines[0], "CA: system certificates (ca.pem not found)");
}

static outcome_kind_t upload_logged(fake_t *fake, const char *root, const config_t *cfg, log_sink_t *log)
{
    experiment_t experiment = experiment_default(cfg->origin);
    session_inputs_t in = {cfg, root, &experiment, log};
    upload_port_t port = port_for(fake);
    return session_upload_test_file(&in, &port);
}

static size_t read_log_text(const log_sink_t *log, char *out, size_t size)
{
    FILE *file = fopen(log->file_path, "rb");
    if (file == NULL) {
        out[0] = '\0';
        return 0;
    }
    size_t count = fread(out, 1, size - 1, file);
    out[count] = '\0';
    fclose(file);
    return count;
}

static size_t count_text(const char *text, const char *needle)
{
    size_t count = 0;
    for (const char *at = strstr(text, needle); at != NULL; at = strstr(at + strlen(needle), needle)) {
        count++;
    }
    return count;
}

static size_t count_lines_containing(const fake_t *fake, const char *needle)
{
    size_t count = 0;
    for (int i = 0; i < fake->line_count; i++) {
        if (strstr(fake->lines[i], needle) != NULL) {
            count++;
        }
    }
    return count;
}

static void make_log_unwritable(char *log_root, size_t size)
{
    char probe[128];
    CHECK(test_fs_make_root(probe, sizeof probe));
    CHECK(test_fs_write_file(probe, "plain-file", "x", 1));
    snprintf(log_root, size, "%s/plain-file", probe);
}

static void upload_writes_one_log_entry_for_a_single_attempt(void)
{
    char root[128];
    char text[16384];
    fake_t fake;
    log_sink_t log;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    log_sink_init(&log, root);
    upload_logged(&fake, root, &cfg, &log);
    read_log_text(&log, text, sizeof text);
    CHECK_INT_EQ(count_text(text, "--- headers ---"), 1);
    CHECK(strstr(text, "HTTP 201") != NULL);
    CHECK(strstr(text, "spike_20261103-142501.zip") != NULL);
}

static void each_retry_attempt_writes_its_own_log_entry(void)
{
    char root[128];
    char text[16384];
    fake_t fake;
    log_sink_t log;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    script_reset_then_success(&fake);
    make_sd_with_test_zip(root, sizeof root);
    log_sink_init(&log, root);
    upload_logged(&fake, root, &cfg, &log);
    read_log_text(&log, text, sizeof text);
    CHECK_INT_EQ(count_text(text, "--- headers ---"), 2);
    CHECK(strstr(text, "attempt 1/2") != NULL);
    CHECK(strstr(text, "attempt 2/2") != NULL);
    CHECK(strstr(text, "curl 56") != NULL);
}

static void session_without_log_sink_still_uploads_and_shows_no_log_warning(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    CHECK_INT_EQ(upload_once(&fake, root, &cfg), OUTCOME_SUCCESS);
    CHECK(!any_line_contains(&fake, "Log unavailable"));
}

static void result_line_contains_variant_tag(void)
{
    char root[128];
    fake_t fake;
    log_sink_t log;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    log_sink_init(&log, root);
    upload_logged(&fake, root, &cfg, &log);
    CHECK(any_line_contains(&fake, "com|noexpect|cl|t120"));
}

static void log_never_contains_upload_body_bytes_headers_or_password(void)
{
    char root[128];
    char text[16384];
    fake_t fake;
    log_sink_t log;
    config_t cfg = sample_config();
    const char *body = "PK-BODY-MARKER-Q9Z7-PK";
    init_fake(&fake, 201);
    snprintf(fake.outcome.headers, sizeof fake.outcome.headers, "Authorization: Basic TE9HSU46VG9wU2VjcmV0QXBwUGFzc3dvcmQ=\r\nX-Echo: %s\r\n", cfg.password);
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, TS_REL_TEST_ZIP, body, strlen(body)));
    log_sink_init(&log, root);
    CHECK_INT_EQ(upload_logged(&fake, root, &cfg, &log), OUTCOME_SUCCESS);
    CHECK(read_log_text(&log, text, sizeof text) > 0);
    CHECK_STR_NOT_CONTAINS(text, "BODY-MARKER");
    CHECK_STR_NOT_CONTAINS(text, cfg.password);
    CHECK_STR_NOT_CONTAINS(text, "TE9HSU46VG9wU2VjcmV0QXBwUGFzc3dvcmQ=");
}

static void unwritable_log_does_not_stop_upload_and_is_reported_once_per_session(void)
{
    char root[128];
    char log_root[256];
    fake_t fake;
    log_sink_t log;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    make_log_unwritable(log_root, sizeof log_root);
    log_sink_init(&log, log_root);
    CHECK_INT_EQ(upload_logged(&fake, root, &cfg, &log), OUTCOME_SUCCESS);
    CHECK_INT_EQ(fake.perform_calls, 1);
    CHECK_INT_EQ(count_lines_containing(&fake, "Log unavailable"), 1);
}

static void log_warning_is_shown_only_once_across_two_uploads(void)
{
    char root[128];
    char log_root[256];
    fake_t fake;
    log_sink_t log;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    make_log_unwritable(log_root, sizeof log_root);
    log_sink_init(&log, log_root);
    upload_logged(&fake, root, &cfg, &log);
    upload_logged(&fake, root, &cfg, &log);
    CHECK_INT_EQ(fake.perform_calls, 2);
    CHECK_INT_EQ(count_lines_containing(&fake, "Log unavailable"), 1);
}

static void log_warning_appears_once_even_when_retry_attempts_fail_to_log(void)
{
    char root[128];
    char log_root[256];
    fake_t fake;
    log_sink_t log;
    config_t cfg = sample_config();
    init_fake(&fake, 201);
    script_reset_then_success(&fake);
    make_sd_with_test_zip(root, sizeof root);
    make_log_unwritable(log_root, sizeof log_root);
    log_sink_init(&log, log_root);
    CHECK_INT_EQ(upload_logged(&fake, root, &cfg, &log), OUTCOME_SUCCESS);
    CHECK_INT_EQ(fake.perform_calls, 2);
    CHECK_INT_EQ(count_lines_containing(&fake, "Log unavailable"), 1);
}

static outcome_kind_t upload_with_experiment(fake_t *fake, const char *root, const config_t *cfg,
                                             const experiment_t *experiment, log_sink_t *log)
{
    session_inputs_t in = {cfg, root, experiment, log};
    upload_port_t port = port_for(fake);
    return session_upload_test_file(&in, &port);
}

static void session_applies_toggled_experiment_parameters_to_the_request(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    experiment_t e = experiment_default(cfg.origin);
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    experiment_toggle(&e, EXP_PARAM_SERVER);
    experiment_toggle(&e, EXP_PARAM_BODY);
    experiment_toggle(&e, EXP_PARAM_TIMEOUTS);
    upload_with_experiment(&fake, root, &cfg, &e, NULL);
    CHECK(strncmp(fake.last_request.url, "https://webdav.yandex.ru/", 25) == 0);
    CHECK_INT_EQ(fake.last_request.content_length, -1);
    CHECK_INT_EQ(fake.last_request.total_timeout_s, 600);
    CHECK(fake.last_request.verify_peer && fake.last_request.verify_host);
}

static void session_result_line_and_log_carry_toggled_variant_tag(void)
{
    char root[128];
    char text[16384];
    fake_t fake;
    log_sink_t log;
    config_t cfg = sample_config();
    experiment_t e = experiment_default(cfg.origin);
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    log_sink_init(&log, root);
    experiment_toggle(&e, EXP_PARAM_SERVER);
    experiment_toggle(&e, EXP_PARAM_EXPECT);
    upload_with_experiment(&fake, root, &cfg, &e, &log);
    read_log_text(&log, text, sizeof text);
    CHECK(any_line_contains(&fake, "ru|expect|cl|t120"));
    CHECK(strstr(text, "ru|expect|cl|t120") != NULL);
}

static void session_with_expect_on_sends_no_expect_header(void)
{
    char root[128];
    fake_t fake;
    config_t cfg = sample_config();
    experiment_t e = experiment_default(cfg.origin);
    init_fake(&fake, 201);
    make_sd_with_test_zip(root, sizeof root);
    experiment_toggle(&e, EXP_PARAM_EXPECT);
    upload_with_experiment(&fake, root, &cfg, &e, NULL);
    CHECK_INT_EQ(fake.last_request.header_count, 0);
}

void run_session_tests(void)
{
    RUN_TEST(session_applies_toggled_experiment_parameters_to_the_request);
    RUN_TEST(session_result_line_and_log_carry_toggled_variant_tag);
    RUN_TEST(session_with_expect_on_sends_no_expect_header);
    RUN_TEST(upload_retries_once_after_connection_reset_and_succeeds);
    RUN_TEST(retry_uses_a_new_remote_name_per_attempt);
    RUN_TEST(upload_fails_after_two_connection_resets_with_clear_message);
    RUN_TEST(upload_retries_on_timeout_resolve_and_connect_errors);
    RUN_TEST(upload_does_not_retry_on_tls_errors);
    RUN_TEST(upload_does_not_retry_on_http_errors);
    RUN_TEST(upload_with_401_shows_wrong_credentials_message);
    RUN_TEST(tls_verification_stays_enabled_on_every_attempt);
    RUN_TEST(each_attempt_shows_uploading_line_before_perform_and_its_own_result_line);
    RUN_TEST(upload_with_empty_test_zip_fails_preflight_with_message);
    RUN_TEST(upload_with_missing_test_zip_shows_not_found_message);
    RUN_TEST(upload_with_oversized_test_zip_fails_preflight_showing_size);
    RUN_TEST(upload_with_unbuildable_request_shows_error_and_performs_nothing);
    RUN_TEST(upload_announces_ca_pem_when_present);
    RUN_TEST(upload_announces_system_certificates_when_ca_pem_absent);
    RUN_TEST(upload_with_201_reports_success);
    RUN_TEST(upload_with_204_reports_success);
    RUN_TEST(upload_with_non_2xx_does_not_report_success);
    RUN_TEST(upload_with_curl_error_does_not_report_success_even_with_2xx_code);
    RUN_TEST(upload_sends_one_request_with_file_content_and_size);
    RUN_TEST(upload_names_remote_file_after_console_clock);
    RUN_TEST(two_uploads_with_different_clock_readings_target_different_urls);
    RUN_TEST(upload_uses_system_certificates_when_ca_pem_absent);
    RUN_TEST(upload_passes_ca_pem_path_when_present_on_sd);
    RUN_TEST(upload_never_shows_password_on_any_output_line);
    RUN_TEST(upload_result_line_shows_http_curl_time_bytes_and_verdict);
    RUN_TEST(upload_failure_line_shows_error_verdict_not_success);
    RUN_TEST(missing_test_zip_fails_preflight_without_network_request);
    RUN_TEST(upload_writes_one_log_entry_for_a_single_attempt);
    RUN_TEST(each_retry_attempt_writes_its_own_log_entry);
    RUN_TEST(session_without_log_sink_still_uploads_and_shows_no_log_warning);
    RUN_TEST(result_line_contains_variant_tag);
    RUN_TEST(log_never_contains_upload_body_bytes_headers_or_password);
    RUN_TEST(unwritable_log_does_not_stop_upload_and_is_reported_once_per_session);
    RUN_TEST(log_warning_is_shown_only_once_across_two_uploads);
    RUN_TEST(log_warning_appears_once_even_when_retry_attempts_fail_to_log);
}
