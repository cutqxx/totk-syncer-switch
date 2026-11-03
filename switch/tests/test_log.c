#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log.h"
#include "redact.h"
#include "test.h"
#include "test_fs.h"

#define USERNAME "LOGIN"
#define PASSWORD "TopSecretAppPassword"
#define BASIC_B64 "TE9HSU46VG9wU2VjcmV0QXBwUGFzc3dvcmQ=" /* base64("LOGIN:TopSecretAppPassword") */
#define URL "https://webdav.yandex.com/JKSV/Game/totk-sync-test-20261103-142501.zip"
#define TAG "com|noexpect|cl|t120"
#define LOG_TEXT_SIZE 16384

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

static upload_outcome_t sample_outcome(void)
{
    upload_outcome_t o = {0};
    o.http_code = 201;
    o.curl_code = 0;
    strcpy(o.curl_error, "No error");
    o.seconds = 12.4;
    o.bytes_sent = 5;
    strcpy(o.headers, "PUT /f HTTP/1.1\r\nHost: webdav.yandex.com\r\nAuthorization: Basic " BASIC_B64 "\r\nContent-Length: 5\r\n\r\nHTTP/1.1 201 Created\r\n");
    return o;
}

static bool read_log(const log_sink_t *log, char *out, size_t size)
{
    FILE *file = fopen(log->file_path, "rb");
    if (file == NULL) {
        return false;
    }
    size_t count = fread(out, 1, size - 1, file);
    out[count] = '\0';
    fclose(file);
    return true;
}

static bool append(log_sink_t *log, int second, const upload_outcome_t *o)
{
    struct tm t = clock_at(second);
    return log_append_attempt(log, &t, TAG, URL, 5, 1, 2, o, USERNAME, PASSWORD);
}

static bool make_unusable_sd_root(char *root, size_t size)
{
    char probe[256];
    if (!test_fs_make_root(probe, sizeof probe)) {
        return false;
    }
    snprintf(root, size, "%s/not-a-directory", probe);
    return test_fs_write_file(probe, "not-a-directory", "x", 1);
}

static size_t count_occurrences(const char *text, const char *needle)
{
    size_t count = 0;
    for (const char *at = strstr(text, needle); at != NULL; at = strstr(at + strlen(needle), needle)) {
        count++;
    }
    return count;
}

static void creates_missing_log_directory_chain_under_sd_root(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    CHECK(append(&log, 1, &o));
    CHECK(read_log(&log, text, sizeof text));
}

static void entry_holds_timestamp_variant_tag_url_and_size(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    append(&log, 1, &o);
    CHECK(read_log(&log, text, sizeof text));
    CHECK(strstr(text, "[2026-11-03 14:25:01]") != NULL);
    CHECK(strstr(text, TAG) != NULL);
    CHECK(strstr(text, URL) != NULL);
    CHECK(strstr(text, "5/5 B") != NULL);
}

static void entry_holds_http_code_curl_code_text_seconds_and_bytes_sent(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    o.http_code = 0;
    o.curl_code = 56;
    strcpy(o.curl_error, "Recv failure: Connection was reset");
    o.seconds = 3.2;
    o.bytes_sent = 4;
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    append(&log, 1, &o);
    CHECK(read_log(&log, text, sizeof text));
    CHECK(strstr(text, "HTTP 0") != NULL);
    CHECK(strstr(text, "curl 56 (Recv failure: Connection was reset)") != NULL);
    CHECK(strstr(text, "3.2 s") != NULL);
    CHECK(strstr(text, "4/5 B") != NULL);
}

static void entry_contains_headers_block_with_redacted_authorization(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    append(&log, 1, &o);
    CHECK(read_log(&log, text, sizeof text));
    CHECK(strstr(text, "PUT /f HTTP/1.1") != NULL);
    CHECK(strstr(text, "Content-Length: 5") != NULL);
    CHECK(strstr(text, "HTTP/1.1 201 Created") != NULL);
    CHECK(strstr(text, "Authorization: ***") != NULL);
}

static void each_call_appends_another_entry(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    CHECK(append(&log, 1, &o));
    CHECK(append(&log, 2, &o));
    CHECK(read_log(&log, text, sizeof text));
    CHECK(strstr(text, "14:25:01") != NULL);
    CHECK(strstr(text, "14:25:02") != NULL);
    CHECK_INT_EQ(count_occurrences(text, "--- headers ---"), 2);
}

static void appending_keeps_entries_of_a_previous_sink_on_the_same_root(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t first;
    log_sink_t second;
    upload_outcome_t o = sample_outcome();
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&first, root);
    append(&first, 1, &o);
    log_sink_init(&second, root);
    append(&second, 2, &o);
    CHECK(read_log(&second, text, sizeof text));
    CHECK(strstr(text, "14:25:01") != NULL);
    CHECK(strstr(text, "14:25:02") != NULL);
}

static void log_never_contains_password_or_its_base64_wherever_it_appears(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    snprintf(o.curl_error, sizeof o.curl_error, "auth failed for %s", PASSWORD);
    strcpy(o.headers,
           "Authorization: Basic " BASIC_B64 "\r\n"
           "X-Echo: " BASIC_B64 "\r\n"
           "X-Debug: password=" PASSWORD "\r\n");
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    CHECK(append(&log, 1, &o));
    CHECK(read_log(&log, text, sizeof text));
    CHECK_STR_NOT_CONTAINS(text, PASSWORD);
    CHECK_STR_NOT_CONTAINS(text, BASIC_B64);
}

static void log_never_contains_password_when_it_is_in_the_url(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    struct tm t = clock_at(1);
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    log_append_attempt(&log, &t, TAG, "https://LOGIN:" PASSWORD "@host/x.zip", 5, 1, 2, &o, USERNAME, PASSWORD);
    CHECK(read_log(&log, text, sizeof text));
    CHECK_STR_NOT_CONTAINS(text, PASSWORD);
}

static void authorization_split_inside_header_name_across_chunks_never_reaches_the_log(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    const char *tail = "ation: Basic " BASIC_B64 "\r\n";
    o.headers[0] = '\0';
    redact_headers_append(o.headers, sizeof o.headers, "Host: h\r\nAuthoriz", 17);
    redact_headers_append(o.headers, sizeof o.headers, tail, strlen(tail));
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    append(&log, 1, &o);
    CHECK(read_log(&log, text, sizeof text));
    CHECK_STR_NOT_CONTAINS(text, BASIC_B64);
    CHECK_STR_NOT_CONTAINS(text, "Basic");
}

static void authorization_split_inside_encoded_value_across_chunks_never_reaches_the_log(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    const char *head = "Authorization: Basic TE9HSU46VG9w";
    const char *tail = "U2VjcmV0QXBwUGFzc3dvcmQ=\r\n";
    o.headers[0] = '\0';
    redact_headers_append(o.headers, sizeof o.headers, head, strlen(head));
    redact_headers_append(o.headers, sizeof o.headers, tail, strlen(tail));
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    append(&log, 1, &o);
    CHECK(read_log(&log, text, sizeof text));
    CHECK_STR_NOT_CONTAINS(text, "U2VjcmV0");
    CHECK_STR_NOT_CONTAINS(text, "TE9HSU46");
}

static void unavailable_log_returns_false_and_warns_exactly_once(void)
{
    char root[256];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    CHECK(make_unusable_sd_root(root, sizeof root));
    log_sink_init(&log, root);
    CHECK(!append(&log, 1, &o));
    CHECK(log_take_warning(&log));
    CHECK(!log_take_warning(&log));
    CHECK(!append(&log, 2, &o));
    CHECK(!log_take_warning(&log));
}

static void healthy_log_never_raises_a_warning(void)
{
    char root[128];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    CHECK(append(&log, 1, &o));
    CHECK(!log_take_warning(&log));
}

static void entry_with_oversized_headers_is_written_without_overflow(void)
{
    char root[128];
    char text[LOG_TEXT_SIZE];
    log_sink_t log;
    upload_outcome_t o = sample_outcome();
    memset(o.headers, 'h', sizeof o.headers - 1);
    o.headers[sizeof o.headers - 1] = '\0';
    CHECK(test_fs_make_root(root, sizeof root));
    log_sink_init(&log, root);
    CHECK(append(&log, 1, &o));
    CHECK(read_log(&log, text, sizeof text));
    CHECK(strstr(text, "hhhhhhhh") != NULL);
}

void run_log_tests(void)
{
    RUN_TEST(creates_missing_log_directory_chain_under_sd_root);
    RUN_TEST(entry_holds_timestamp_variant_tag_url_and_size);
    RUN_TEST(entry_holds_http_code_curl_code_text_seconds_and_bytes_sent);
    RUN_TEST(entry_contains_headers_block_with_redacted_authorization);
    RUN_TEST(each_call_appends_another_entry);
    RUN_TEST(appending_keeps_entries_of_a_previous_sink_on_the_same_root);
    RUN_TEST(log_never_contains_password_or_its_base64_wherever_it_appears);
    RUN_TEST(log_never_contains_password_when_it_is_in_the_url);
    RUN_TEST(authorization_split_inside_header_name_across_chunks_never_reaches_the_log);
    RUN_TEST(authorization_split_inside_encoded_value_across_chunks_never_reaches_the_log);
    RUN_TEST(unavailable_log_returns_false_and_warns_exactly_once);
    RUN_TEST(healthy_log_never_raises_a_warning);
    RUN_TEST(entry_with_oversized_headers_is_written_without_overflow);
}
