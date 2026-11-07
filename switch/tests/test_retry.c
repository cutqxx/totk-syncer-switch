#include <stdio.h>
#include <string.h>

#include "outcome.h"
#include "retry.h"
#include "test.h"
#include "test_support.h"

static void classify_maps_curl_codes_to_transport_kinds(void)
{
    CHECK_INT_EQ(outcome_classify(0), TRANSPORT_OK);
    CHECK_INT_EQ(outcome_classify(6), TRANSPORT_UNREACHABLE);
    CHECK_INT_EQ(outcome_classify(7), TRANSPORT_UNREACHABLE);
    CHECK_INT_EQ(outcome_classify(28), TRANSPORT_UNREACHABLE);
    CHECK_INT_EQ(outcome_classify(56), TRANSPORT_DROPPED);
    CHECK_INT_EQ(outcome_classify(35), TRANSPORT_OTHER);
    CHECK_INT_EQ(outcome_classify(-1), TRANSPORT_OTHER);
}

static void retries_only_transient_codes_while_attempts_remain(void)
{
    static const int transient[] = { 6, 7, 28, 56 };
    for (size_t i = 0; i < sizeof transient / sizeof transient[0]; i++) {
        CHECK(outcome_should_retry(transient[i], 1, 2));
        CHECK(!outcome_should_retry(transient[i], 2, 2));
    }
}

static void never_retries_success_or_other_curl_errors(void)
{
    CHECK(!outcome_should_retry(0, 1, 2));
    CHECK(!outcome_should_retry(35, 1, 2));
    CHECK(!outcome_should_retry(60, 1, 2));
}

static void a_transient_curl_failure_is_retried_once_after_3_seconds(void)
{
    static const int transient[] = { 6, 7, 28, 56 };
    for (size_t i = 0; i < sizeof transient / sizeof transient[0]; i++) {
        const scripted_reply_t script[] = { { 0, transient[i], NULL, NULL }, { 200, 0, LAUNCHER_MARKER_HEADERS, "{}" } };
        fake_world_t world;
        http_request_t req;
        http_response_t out;
        fake_world_init(&world, script, 2);
        http_request_init(&req, HTTP_GET);
        retry_perform(&world.env, &req, &out);
        CHECK_INT_EQ(world.net_calls, 2);
        CHECK_INT_EQ(world.sleep_count, 1);
        CHECK_INT_EQ(world.slept_seconds[0], 3);
        CHECK_INT_EQ(out.curl_code, 0);
        CHECK_INT_EQ(out.http_code, 200);
        http_response_free(&out);
    }
}

static void the_retry_is_announced_on_screen(void)
{
    const scripted_reply_t script[] = { { 0, 7, NULL, NULL }, { 200, 0, NULL, NULL } };
    fake_world_t world;
    http_request_t req;
    http_response_t out;
    fake_world_init(&world, script, 2);
    http_request_init(&req, HTTP_GET);
    retry_perform(&world.env, &req, &out);
    CHECK_INT_EQ(world.ui_line_count, 1);
    CHECK_STR_EQ(world.ui_lines[0], "Retrying in 3 s...");
    http_response_free(&out);
}

static void two_failures_give_the_last_failure_and_no_third_attempt(void)
{
    const scripted_reply_t script[] = { { 0, 7, NULL, NULL }, { 0, 28, NULL, NULL }, { 200, 0, NULL, NULL } };
    fake_world_t world;
    http_request_t req;
    http_response_t out;
    fake_world_init(&world, script, 3);
    http_request_init(&req, HTTP_GET);
    retry_perform(&world.env, &req, &out);
    CHECK_INT_EQ(world.net_calls, 2);
    CHECK_INT_EQ(world.sleep_count, 1);
    CHECK_INT_EQ(out.curl_code, 28);
    http_response_free(&out);
}

static void http_status_codes_are_never_retried_including_401(void)
{
    static const long codes[] = { 200, 401, 403, 404, 500, 503 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        const scripted_reply_t script[] = { { codes[i], 0, NULL, NULL } };
        fake_world_t world;
        http_request_t req;
        http_response_t out;
        fake_world_init(&world, script, 1);
        http_request_init(&req, HTTP_GET);
        retry_perform(&world.env, &req, &out);
        CHECK_INT_EQ(world.net_calls, 1);
        CHECK_INT_EQ(world.sleep_count, 0);
        CHECK_INT_EQ(world.ui_line_count, 0);
        CHECK_INT_EQ(out.http_code, codes[i]);
        http_response_free(&out);
    }
}

static void non_transient_curl_errors_are_not_retried(void)
{
    const scripted_reply_t script[] = { { 0, 35, NULL, NULL }, { 200, 0, NULL, NULL } };
    fake_world_t world;
    http_request_t req;
    http_response_t out;
    fake_world_init(&world, script, 2);
    http_request_init(&req, HTTP_GET);
    retry_perform(&world.env, &req, &out);
    CHECK_INT_EQ(world.net_calls, 1);
    CHECK_INT_EQ(out.curl_code, 35);
    http_response_free(&out);
}

static void the_retry_sends_the_same_request(void)
{
    const scripted_reply_t script[] = { { 0, 7, NULL, NULL }, { 200, 0, NULL, NULL } };
    fake_world_t world;
    http_request_t req;
    http_response_t out;
    fake_world_init(&world, script, 2);
    http_request_init(&req, HTTP_GET);
    snprintf(req.url, sizeof req.url, "http://10.0.0.5:9000/status");
    http_request_add_header(&req, "Authorization: Bearer abc");
    retry_perform(&world.env, &req, &out);
    CHECK_STR_EQ(world.last_request.url, "http://10.0.0.5:9000/status");
    CHECK_STR_EQ(world.last_request.headers[0], "Authorization: Bearer abc");
    CHECK_INT_EQ(world.last_request.connect_timeout_s, 5);
    CHECK_INT_EQ(world.last_request.total_timeout_s, 60);
    http_response_free(&out);
}

static void the_result_holds_only_the_last_attempt_body(void)
{
    const scripted_reply_t script[] = { { 0, 56, NULL, "partial" }, { 200, 0, NULL, "full" } };
    fake_world_t world;
    http_request_t req;
    http_response_t out;
    fake_world_init(&world, script, 2);
    http_request_init(&req, HTTP_GET);
    retry_perform(&world.env, &req, &out);
    CHECK_INT_EQ(out.body.size, 4);
    CHECK_STR_EQ((const char *)out.body.data, "full");
    http_response_free(&out);
}

void run_retry_tests(void)
{
    RUN_TEST(classify_maps_curl_codes_to_transport_kinds);
    RUN_TEST(retries_only_transient_codes_while_attempts_remain);
    RUN_TEST(never_retries_success_or_other_curl_errors);
    RUN_TEST(a_transient_curl_failure_is_retried_once_after_3_seconds);
    RUN_TEST(the_retry_is_announced_on_screen);
    RUN_TEST(two_failures_give_the_last_failure_and_no_third_attempt);
    RUN_TEST(http_status_codes_are_never_retried_including_401);
    RUN_TEST(non_transient_curl_errors_are_not_retried);
    RUN_TEST(the_retry_sends_the_same_request);
    RUN_TEST(the_result_holds_only_the_last_attempt_body);
}
