#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "request.h"
#include "test.h"

static config_t sample_config(void)
{
    config_t cfg = {0};
    strcpy(cfg.origin, "https://webdav.yandex.com");
    strcpy(cfg.basepath, "JKSV/The Legend of Zelda Tears of the Kingdom");
    strcpy(cfg.username, "LOGIN");
    strcpy(cfg.password, "APP_PASSWORD");
    return cfg;
}

static void put_request_targets_full_encoded_destination_url(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    CHECK(request_build_put(&cfg, "spike_20261103-142501.zip", 100, NULL, NULL, &req));
    CHECK_STR_EQ(req.url, "https://webdav.yandex.com/JKSV/The%20Legend%20of%20Zelda%20Tears%20of%20the%20Kingdom/spike_20261103-142501.zip");
}

static void put_request_declares_content_length_equal_to_body_size(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    CHECK(request_build_put(&cfg, "a.zip", 4194304, NULL, NULL, &req));
    CHECK_INT_EQ(req.content_length, 4194304);
}

static void put_request_keeps_content_length_above_4_gib(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    CHECK(request_build_put(&cfg, "a.zip", 5000000000LL, NULL, NULL, &req));
    CHECK_INT_EQ(req.content_length, 5000000000LL);
}

static void put_request_sends_only_empty_expect_header_and_no_chunked_encoding(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, NULL, &req));
    CHECK_INT_EQ(req.header_count, 1);
    CHECK_STR_EQ(req.headers[0], "Expect:");
    for (int i = 0; i < req.header_count; i++) {
        CHECK(strstr(req.headers[i], "Transfer-Encoding") == NULL);
        CHECK(strstr(req.headers[i], "chunked") == NULL);
    }
}

static void put_request_uses_http_1_1(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, NULL, &req));
    CHECK(req.http_1_1);
}

static void put_request_verifies_tls_peer_and_host(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, NULL, &req));
    CHECK(req.verify_peer);
    CHECK(req.verify_host);
}

static void put_request_uses_15s_connect_and_120s_total_timeouts(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, NULL, &req));
    CHECK_INT_EQ(req.connect_timeout_s, 15);
    CHECK_INT_EQ(req.total_timeout_s, 120);
}

static void put_request_carries_basic_auth_credentials(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, NULL, &req));
    CHECK_STR_EQ(req.username, "LOGIN");
    CHECK_STR_EQ(req.password, "APP_PASSWORD");
}

static void put_request_ca_file_is_empty_when_not_given(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    memset(&req, 'x', sizeof req);
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, NULL, &req));
    CHECK_STR_EQ(req.ca_file, "");
}

static void put_request_copies_ca_file_path_when_given(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    CHECK(request_build_put(&cfg, "a.zip", 1, "sd/switch/totk-sync/ca.pem", NULL, &req));
    CHECK_STR_EQ(req.ca_file, "sd/switch/totk-sync/ca.pem");
}

static void put_request_fails_when_ca_file_path_does_not_fit(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    char long_path[600];
    memset(long_path, 'a', sizeof long_path - 1);
    long_path[sizeof long_path - 1] = '\0';
    CHECK(!request_build_put(&cfg, "a.zip", 1, long_path, NULL, &req));
}

static void put_request_fails_when_encoded_url_exceeds_buffer(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    memset(cfg.basepath, ' ', 400); /* each space becomes %20: 1200 bytes > 1024 */
    cfg.basepath[400] = '\0';
    CHECK(!request_build_put(&cfg, "a.zip", 1, NULL, NULL, &req));
}

static const char *const ENCODED_TAIL =
    "/JKSV/The%20Legend%20of%20Zelda%20Tears%20of%20the%20Kingdom/a.zip";

static bool has_header(const upload_request_t *req, const char *header)
{
    for (int i = 0; i < req->header_count; i++) {
        if (strcmp(req->headers[i], header) == 0) return true;
    }
    return false;
}

static bool has_header_starting_with(const upload_request_t *req, const char *prefix)
{
    for (int i = 0; i < req->header_count; i++) {
        if (strncmp(req->headers[i], prefix, strlen(prefix)) == 0) return true;
    }
    return false;
}

static void expect_url_is(const upload_request_t *req, const char *origin)
{
    char expected[1024];
    snprintf(expected, sizeof expected, "%s%s", origin, ENCODED_TAIL);
    CHECK_STR_EQ(req->url, expected);
}

static void experiment_ru_server_changes_only_the_origin_of_the_url(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    experiment_t e = experiment_default(cfg.origin);
    experiment_toggle(&e, EXP_PARAM_SERVER);
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, &e, &req));
    expect_url_is(&req, "https://webdav.yandex.ru");
}

static void experiment_server_toggled_back_restores_com_url(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    experiment_t e = experiment_default(cfg.origin);
    experiment_toggle(&e, EXP_PARAM_SERVER);
    experiment_toggle(&e, EXP_PARAM_SERVER);
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, &e, &req));
    expect_url_is(&req, "https://webdav.yandex.com");
}

static void experiment_other_config_origin_is_used_as_is_by_default(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    strcpy(cfg.origin, "https://dav.example.com");
    experiment_t e = experiment_default(cfg.origin);
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, &e, &req));
    expect_url_is(&req, "https://dav.example.com");
}

static void experiment_server_toggle_from_other_config_uses_yandex_com(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    strcpy(cfg.origin, "https://dav.example.com");
    experiment_t e = experiment_default(cfg.origin);
    experiment_toggle(&e, EXP_PARAM_SERVER);
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, &e, &req));
    expect_url_is(&req, "https://webdav.yandex.com");
}

static void experiment_expect_on_sends_no_expect_header_at_all(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    experiment_t e = experiment_default(cfg.origin);
    experiment_toggle(&e, EXP_PARAM_EXPECT);
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, &e, &req));
    CHECK(!has_header_starting_with(&req, "Expect"));
    CHECK_INT_EQ(req.header_count, 0);
}

static void experiment_expect_off_sends_exactly_empty_expect_header(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    experiment_t e = experiment_default(cfg.origin);
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, &e, &req));
    CHECK(has_header(&req, "Expect:"));
    CHECK(!has_header_starting_with(&req, "Transfer-Encoding"));
}

static void experiment_chunked_body_has_unknown_length_and_chunked_header(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    experiment_t e = experiment_default(cfg.origin);
    experiment_toggle(&e, EXP_PARAM_BODY);
    CHECK(request_build_put(&cfg, "a.zip", 4096, NULL, &e, &req));
    CHECK_INT_EQ(req.content_length, -1);
    CHECK(has_header(&req, "Transfer-Encoding: chunked"));
    CHECK(has_header(&req, "Expect:"));
}

static void experiment_long_timeouts_are_30_and_600(void)
{
    config_t cfg = sample_config();
    upload_request_t req;
    experiment_t e = experiment_default(cfg.origin);
    experiment_toggle(&e, EXP_PARAM_TIMEOUTS);
    CHECK(request_build_put(&cfg, "a.zip", 1, NULL, &e, &req));
    CHECK_INT_EQ(req.connect_timeout_s, 30);
    CHECK_INT_EQ(req.total_timeout_s, 600);
}

static void every_experiment_combination_keeps_tls_verification_and_valid_request(void)
{
    config_t cfg = sample_config();
    for (int mask = 0; mask < 16; mask++) {
        experiment_t e = experiment_default(cfg.origin);
        for (int bit = 0; bit < 4; bit++) {
            if (mask & (1 << bit)) experiment_toggle(&e, (exp_param_t)bit);
        }
        upload_request_t req;
        CHECK(request_build_put(&cfg, "a.zip", 100, NULL, &e, &req));
        CHECK(req.verify_peer && req.verify_host);
        CHECK(req.header_count >= 0 && req.header_count <= TS_REQ_MAX_HEADERS);
        CHECK(strncmp(req.url, "https://webdav.yandex.", 22) == 0);
        CHECK(strstr(req.url, ENCODED_TAIL) != NULL);
        CHECK_INT_EQ(req.content_length, (mask & 4) ? -1 : 100);
        CHECK_INT_EQ(req.connect_timeout_s, (mask & 8) ? 30 : 15);
    }
}

static void null_experiment_equals_default_experiment_request(void)
{
    config_t cfg = sample_config();
    upload_request_t with_null, with_default;
    experiment_t e = experiment_default(cfg.origin);
    CHECK(request_build_put(&cfg, "a.zip", 7, NULL, NULL, &with_null));
    CHECK(request_build_put(&cfg, "a.zip", 7, NULL, &e, &with_default));
    CHECK_STR_EQ(with_null.url, with_default.url);
    CHECK_INT_EQ(with_null.content_length, with_default.content_length);
    CHECK_INT_EQ(with_null.header_count, with_default.header_count);
    CHECK_INT_EQ(with_null.total_timeout_s, with_default.total_timeout_s);
}

void run_request_tests(void)
{
    RUN_TEST(experiment_ru_server_changes_only_the_origin_of_the_url);
    RUN_TEST(experiment_server_toggled_back_restores_com_url);
    RUN_TEST(experiment_other_config_origin_is_used_as_is_by_default);
    RUN_TEST(experiment_server_toggle_from_other_config_uses_yandex_com);
    RUN_TEST(experiment_expect_on_sends_no_expect_header_at_all);
    RUN_TEST(experiment_expect_off_sends_exactly_empty_expect_header);
    RUN_TEST(experiment_chunked_body_has_unknown_length_and_chunked_header);
    RUN_TEST(experiment_long_timeouts_are_30_and_600);
    RUN_TEST(every_experiment_combination_keeps_tls_verification_and_valid_request);
    RUN_TEST(null_experiment_equals_default_experiment_request);
    RUN_TEST(put_request_targets_full_encoded_destination_url);
    RUN_TEST(put_request_declares_content_length_equal_to_body_size);
    RUN_TEST(put_request_keeps_content_length_above_4_gib);
    RUN_TEST(put_request_sends_only_empty_expect_header_and_no_chunked_encoding);
    RUN_TEST(put_request_uses_http_1_1);
    RUN_TEST(put_request_verifies_tls_peer_and_host);
    RUN_TEST(put_request_uses_15s_connect_and_120s_total_timeouts);
    RUN_TEST(put_request_carries_basic_auth_credentials);
    RUN_TEST(put_request_ca_file_is_empty_when_not_given);
    RUN_TEST(put_request_copies_ca_file_path_when_given);
    RUN_TEST(put_request_fails_when_ca_file_path_does_not_fit);
    RUN_TEST(put_request_fails_when_encoded_url_exceeds_buffer);
}
