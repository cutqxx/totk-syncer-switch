#include <stdio.h>
#include <string.h>

#include "http.h"
#include "test.h"

static void request_init_sets_5s_connect_60s_total_and_json_limit(void)
{
    http_request_t req;
    http_request_init(&req, HTTP_GET);
    CHECK_INT_EQ(req.method, HTTP_GET);
    CHECK_INT_EQ(req.connect_timeout_s, 5);
    CHECK_INT_EQ(req.total_timeout_s, 60);
    CHECK_INT_EQ(req.max_response_bytes, 64 * 1024);
    CHECK_INT_EQ(req.header_count, 0);
    CHECK(req.body == NULL);
    CHECK_STR_EQ(req.url, "");
}

static void add_header_stores_lines_in_order(void)
{
    http_request_t req;
    http_request_init(&req, HTTP_PUT);
    http_request_add_header(&req, "A: 1");
    http_request_add_header(&req, "B: 2");
    CHECK_INT_EQ(req.header_count, 2);
    CHECK_STR_EQ(req.headers[0], "A: 1");
    CHECK_STR_EQ(req.headers[1], "B: 2");
}

static void add_header_ignores_lines_beyond_the_capacity(void)
{
    http_request_t req;
    http_request_init(&req, HTTP_GET);
    for (int i = 0; i < TS_MAX_HEADERS + 3; i++) {
        http_request_add_header(&req, "H: v");
    }
    CHECK_INT_EQ(req.header_count, TS_MAX_HEADERS);
}

static void add_header_truncates_an_overlong_line_with_a_terminator(void)
{
    http_request_t req;
    char longline[TS_HEADER_LINE_SIZE * 2];
    memset(longline, 'x', sizeof longline);
    longline[sizeof longline - 1] = '\0';
    http_request_init(&req, HTTP_GET);
    http_request_add_header(&req, longline);
    CHECK_INT_EQ((long long)strlen(req.headers[0]), TS_HEADER_LINE_SIZE - 1);
}

static void method_names_are_the_http_verbs(void)
{
    CHECK_STR_EQ(http_method_name(HTTP_GET), "GET");
    CHECK_STR_EQ(http_method_name(HTTP_PUT), "PUT");
    CHECK_STR_EQ(http_method_name(HTTP_POST), "POST");
}

static void has_header_matches_name_case_insensitively_and_value_exactly(void)
{
    http_response_t resp;
    http_response_init(&resp);
    snprintf(resp.response_headers, sizeof resp.response_headers, "HTTP/1.1 200 OK\r\nx-totksync-api:   1  \r\nOther: 12\r\n\r\n");
    CHECK(http_response_has_header(&resp, "X-TotkSync-Api", "1"));
    CHECK(!http_response_has_header(&resp, "X-TotkSync-Api", "12"));
    CHECK(!http_response_has_header(&resp, "Other", "1"));
    CHECK(http_response_has_header(&resp, "other", "12"));
    CHECK(!http_response_has_header(&resp, "Missing", "1"));
}

static void has_header_does_not_match_a_name_that_is_only_a_prefix(void)
{
    http_response_t resp;
    http_response_init(&resp);
    snprintf(resp.response_headers, sizeof resp.response_headers, "X-TotkSync-Api-Extra: 1\r\nX-TotkSync: 1\r\n");
    CHECK(!http_response_has_header(&resp, "X-TotkSync-Api", "1"));
}

static void has_header_works_on_lines_ending_with_bare_lf_and_without_final_newline(void)
{
    http_response_t resp;
    http_response_init(&resp);
    snprintf(resp.response_headers, sizeof resp.response_headers, "HTTP/1.1 200 OK\nA: 1\nX-TotkSync-Api: 1");
    CHECK(http_response_has_header(&resp, "X-TotkSync-Api", "1"));
}

static void has_header_is_false_for_empty_headers(void)
{
    http_response_t resp;
    http_response_init(&resp);
    CHECK(!http_response_has_header(&resp, "X-TotkSync-Api", "1"));
}

static void header_text_in_the_body_is_not_a_header(void)
{
    http_response_t resp;
    http_response_init(&resp);
    body_append(&resp.body, "X-TotkSync-Api: 1\r\n", 19, 100);
    CHECK(!http_response_has_header(&resp, "X-TotkSync-Api", "1"));
    http_response_free(&resp);
}

void run_http_tests(void)
{
    RUN_TEST(request_init_sets_5s_connect_60s_total_and_json_limit);
    RUN_TEST(add_header_stores_lines_in_order);
    RUN_TEST(add_header_ignores_lines_beyond_the_capacity);
    RUN_TEST(add_header_truncates_an_overlong_line_with_a_terminator);
    RUN_TEST(method_names_are_the_http_verbs);
    RUN_TEST(has_header_matches_name_case_insensitively_and_value_exactly);
    RUN_TEST(has_header_does_not_match_a_name_that_is_only_a_prefix);
    RUN_TEST(has_header_works_on_lines_ending_with_bare_lf_and_without_final_newline);
    RUN_TEST(has_header_is_false_for_empty_headers);
    RUN_TEST(header_text_in_the_body_is_not_a_header);
}
