#include <string.h>

#include "outcome.h"
#include "test.h"

static upload_outcome_t http_result(long http_code)
{
    upload_outcome_t o = {0};
    o.http_code = http_code;
    strcpy(o.curl_error, "No error");
    return o;
}

static upload_outcome_t curl_failure(int curl_code, const char *text)
{
    upload_outcome_t o = {0};
    o.curl_code = curl_code;
    strncpy(o.curl_error, text, sizeof o.curl_error - 1);
    return o;
}

static void expect_kind_for_http(long http_code, outcome_kind_t expected)
{
    upload_outcome_t o = http_result(http_code);
    CHECK_INT_EQ(outcome_classify(&o), expected);
}

static void expect_kind_for_curl(int curl_code, outcome_kind_t expected)
{
    upload_outcome_t o = curl_failure(curl_code, "x");
    CHECK_INT_EQ(outcome_classify(&o), expected);
}

static void expect_message_for_http(long http_code, const char *expected)
{
    char msg[256];
    upload_outcome_t o = http_result(http_code);
    outcome_message(&o, msg, sizeof msg);
    CHECK_STR_EQ(msg, expected);
}

static void expect_message_for_curl(int curl_code, const char *expected)
{
    char msg[256];
    upload_outcome_t o = curl_failure(curl_code, "curl text");
    outcome_message(&o, msg, sizeof msg);
    CHECK_STR_EQ(msg, expected);
}

static void classifies_2xx_range_boundaries_as_success(void)
{
    expect_kind_for_http(200, OUTCOME_SUCCESS);
    expect_kind_for_http(201, OUTCOME_SUCCESS);
    expect_kind_for_http(204, OUTCOME_SUCCESS);
    expect_kind_for_http(299, OUTCOME_SUCCESS);
}

static void classifies_codes_just_outside_2xx_as_failure(void)
{
    expect_kind_for_http(199, OUTCOME_HTTP_OTHER);
    expect_kind_for_http(300, OUTCOME_HTTP_OTHER);
    expect_kind_for_http(0, OUTCOME_HTTP_OTHER);
}

static void classifies_specific_http_errors(void)
{
    expect_kind_for_http(401, OUTCOME_AUTH);
    expect_kind_for_http(409, OUTCOME_DIR_MISSING);
    expect_kind_for_http(507, OUTCOME_NO_SPACE);
    expect_kind_for_http(500, OUTCOME_HTTP_OTHER);
    expect_kind_for_http(404, OUTCOME_HTTP_OTHER);
}

static void classifies_curl_codes(void)
{
    expect_kind_for_curl(6, OUTCOME_NET_RESOLVE);
    expect_kind_for_curl(7, OUTCOME_NET_CONNECT);
    expect_kind_for_curl(28, OUTCOME_TIMEOUT);
    expect_kind_for_curl(56, OUTCOME_NET_RECV);
    expect_kind_for_curl(35, OUTCOME_TLS);
    expect_kind_for_curl(60, OUTCOME_TLS);
    expect_kind_for_curl(23, OUTCOME_CURL_OTHER);
}

static void curl_error_beats_2xx_http_code(void)
{
    upload_outcome_t o = curl_failure(56, "Recv failure");
    o.http_code = 201;
    CHECK_INT_EQ(outcome_classify(&o), OUTCOME_NET_RECV);
}

static void curl_error_beats_401_http_code(void)
{
    upload_outcome_t o = curl_failure(7, "refused");
    o.http_code = 401;
    CHECK_INT_EQ(outcome_classify(&o), OUTCOME_NET_CONNECT);
}

static void message_for_success_includes_http_code(void)
{
    expect_message_for_http(201, "File uploaded (HTTP 201)");
}

static void message_for_http_errors_is_specific_per_status_code(void)
{
    expect_message_for_http(401, "Wrong login/password (401)");
    expect_message_for_http(409, "Folder not found on Disk (409)");
    expect_message_for_http(507, "Not enough space on Disk (507)");
    expect_message_for_http(500, "Server error: HTTP 500");
    expect_message_for_http(418, "Server error: HTTP 418");
}

static void message_for_network_and_tls_curl_codes_is_specific_and_includes_code(void)
{
    expect_message_for_curl(6, "No network / cannot resolve server (6)");
    expect_message_for_curl(7, "Cannot connect (7)");
    expect_message_for_curl(28, "Timeout (28)");
    expect_message_for_curl(56, "Connection dropped during transfer (56)");
    expect_message_for_curl(35, "TLS / certificate verification error (35)");
    expect_message_for_curl(60, "TLS / certificate verification error (60)");
}

static void message_for_unknown_curl_code_includes_code_and_curl_text(void)
{
    expect_message_for_curl(23, "curl error 23: curl text");
}

static void message_is_truncated_and_terminated_in_small_buffer(void)
{
    char msg[8];
    upload_outcome_t o = http_result(401);
    outcome_message(&o, msg, sizeof msg);
    CHECK_INT_EQ(strlen(msg), sizeof msg - 1);
}

static void retries_transient_network_kinds_while_attempts_remain(void)
{
    outcome_kind_t transient[] = {OUTCOME_NET_RESOLVE, OUTCOME_NET_CONNECT, OUTCOME_TIMEOUT, OUTCOME_NET_RECV};
    for (size_t i = 0; i < sizeof transient / sizeof transient[0]; i++) {
        CHECK(outcome_should_retry(transient[i], 1, 2));
    }
}

static void does_not_retry_transient_kinds_on_last_attempt(void)
{
    outcome_kind_t transient[] = {OUTCOME_NET_RESOLVE, OUTCOME_NET_CONNECT, OUTCOME_TIMEOUT, OUTCOME_NET_RECV};
    for (size_t i = 0; i < sizeof transient / sizeof transient[0]; i++) {
        CHECK(!outcome_should_retry(transient[i], 2, 2));
        CHECK(!outcome_should_retry(transient[i], 3, 2));
    }
}

static void never_retries_non_transient_kinds(void)
{
    outcome_kind_t fatal[] = {OUTCOME_SUCCESS, OUTCOME_AUTH, OUTCOME_DIR_MISSING, OUTCOME_NO_SPACE,
                              OUTCOME_HTTP_OTHER, OUTCOME_TLS, OUTCOME_CURL_OTHER, OUTCOME_PREFLIGHT_FAILED};
    for (size_t i = 0; i < sizeof fatal / sizeof fatal[0]; i++) {
        CHECK(!outcome_should_retry(fatal[i], 1, 2));
    }
}

static void format_line_includes_attempt_counter_and_timestamp(void)
{
    char line[256];
    struct tm t = {0};
    upload_outcome_t o = http_result(201);
    t.tm_year = 126;
    t.tm_mon = 10;
    t.tm_mday = 3;
    outcome_format_line(&t, NULL, 2, 2, 5, &o, line, sizeof line);
    CHECK(strstr(line, "attempt 2/2") != NULL);
    CHECK(strstr(line, "[2026-11-03 00:00:00]") != NULL);
}

static void format_line_includes_variant_tag_when_given(void)
{
    char line[256];
    struct tm t = {0};
    upload_outcome_t o = http_result(201);
    outcome_format_line(&t, "TAGX", 1, 2, 5, &o, line, sizeof line);
    CHECK(strstr(line, "] TAGX attempt") != NULL);
}

void run_outcome_tests(void)
{
    RUN_TEST(classifies_2xx_range_boundaries_as_success);
    RUN_TEST(classifies_codes_just_outside_2xx_as_failure);
    RUN_TEST(classifies_specific_http_errors);
    RUN_TEST(classifies_curl_codes);
    RUN_TEST(curl_error_beats_2xx_http_code);
    RUN_TEST(curl_error_beats_401_http_code);
    RUN_TEST(message_for_success_includes_http_code);
    RUN_TEST(message_for_http_errors_is_specific_per_status_code);
    RUN_TEST(message_for_network_and_tls_curl_codes_is_specific_and_includes_code);
    RUN_TEST(message_for_unknown_curl_code_includes_code_and_curl_text);
    RUN_TEST(message_is_truncated_and_terminated_in_small_buffer);
    RUN_TEST(retries_transient_network_kinds_while_attempts_remain);
    RUN_TEST(does_not_retry_transient_kinds_on_last_attempt);
    RUN_TEST(never_retries_non_transient_kinds);
    RUN_TEST(format_line_includes_attempt_counter_and_timestamp);
    RUN_TEST(format_line_includes_variant_tag_when_given);
}
