#include <stdio.h>
#include <string.h>

#include "pc_status.h"
#include "test.h"
#include "test_support.h"

static api_status_t answer_of(api_status_kind_t kind)
{
    api_status_t answer;
    memset(&answer, 0, sizeof answer);
    answer.kind = kind;
    return answer;
}

static void describe(const api_status_t *answer, pc_status_t *out)
{
    config_t cfg;
    test_make_config(&cfg);
    pc_status_describe(answer, &cfg, out);
}

static void online_with_a_save_shows_its_time(void)
{
    api_status_t answer = answer_of(API_STATUS_OK);
    pc_status_t st;
    answer.save_present = true;
    snprintf(answer.save_modified, sizeof answer.save_modified, "2026-11-03 14:25");
    describe(&answer, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_ONLINE);
    CHECK_STR_EQ(st.status_line, "PC: online, save from 2026-11-03 14:25");
    CHECK_STR_EQ(st.detail, "");
}

static void online_without_a_save_says_no_save(void)
{
    api_status_t answer = answer_of(API_STATUS_OK);
    pc_status_t st;
    describe(&answer, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_ONLINE);
    CHECK_STR_EQ(st.status_line, "PC: online, no save");
    CHECK_STR_EQ(st.detail, "");
}

static void a_running_game_wins_over_a_present_save(void)
{
    api_status_t answer = answer_of(API_STATUS_OK);
    pc_status_t st;
    answer.game_running = true;
    answer.save_present = true;
    snprintf(answer.save_modified, sizeof answer.save_modified, "2026-11-03 14:25");
    describe(&answer, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_ONLINE);
    CHECK_STR_EQ(st.status_line, "PC: TotK is running");
}

static void a_running_game_wins_over_a_missing_save(void)
{
    api_status_t answer = answer_of(API_STATUS_OK);
    pc_status_t st;
    answer.game_running = true;
    describe(&answer, &st);
    CHECK_STR_EQ(st.status_line, "PC: TotK is running");
}

static void unreachable_shows_the_status_line_and_the_hint(void)
{
    api_status_t answer = answer_of(API_STATUS_UNREACHABLE);
    pc_status_t st;
    describe(&answer, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_NOT_REACHABLE);
    CHECK_STR_EQ(st.status_line, "PC: not reachable");
    CHECK(strncmp(st.detail, "PC not reachable: is the launcher open? check firewall", 54) == 0);
}

static void wrong_token_has_the_spec_text(void)
{
    api_status_t answer = answer_of(API_STATUS_WRONG_TOKEN);
    pc_status_t st;
    describe(&answer, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_ERROR);
    CHECK_STR_EQ(st.status_line, "PC: error");
    CHECK_STR_EQ(st.detail, "Wrong token: save switch-config.json from the PC launcher");
}

static void not_a_launcher_names_the_host_and_port(void)
{
    api_status_t answer = answer_of(API_STATUS_NOT_LAUNCHER);
    pc_status_t st;
    describe(&answer, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_ERROR);
    CHECK_STR_EQ(st.detail, "Not a TotK Sync launcher at 192.168.1.10:8765");
}

static void other_http_codes_are_reported_with_the_code(void)
{
    static const long codes[] = { 404, 405, 500, 503 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        api_status_t answer = answer_of(API_STATUS_HTTP_ERROR);
        pc_status_t st;
        char expected[64];
        answer.http_code = codes[i];
        describe(&answer, &st);
        snprintf(expected, sizeof expected, "PC error: HTTP %ld", codes[i]);
        CHECK_INT_EQ(st.kind, PC_STATUS_ERROR);
        CHECK_STR_EQ(st.detail, expected);
    }
}

static void forbidden_mentions_http_403_and_the_private_network(void)
{
    api_status_t answer = answer_of(API_STATUS_FORBIDDEN);
    pc_status_t st;
    answer.http_code = 403;
    describe(&answer, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_ERROR);
    CHECK(strstr(st.detail, "HTTP 403") != NULL);
    CHECK(strstr(st.detail, "private network") != NULL);
}

static void a_malformed_launcher_answer_is_a_pc_error(void)
{
    api_status_t answer = answer_of(API_STATUS_BAD_RESPONSE);
    pc_status_t st;
    describe(&answer, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_ERROR);
    CHECK_STR_EQ(st.detail, "PC error: unexpected status response");
}

static void other_network_failures_show_the_curl_code(void)
{
    api_status_t answer = answer_of(API_STATUS_NETWORK_ERROR);
    pc_status_t st;
    answer.curl_code = 56;
    describe(&answer, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_ERROR);
    CHECK_STR_EQ(st.detail, "Network error: curl 56");
}

static void describe_overwrites_stale_output(void)
{
    api_status_t answer = answer_of(API_STATUS_OK);
    pc_status_t st;
    memset(&st, 'z', sizeof st);
    describe(&answer, &st);
    CHECK_STR_EQ(st.detail, "");
}

static void describe_removes_the_token_if_it_ever_lands_in_the_text(void)
{
    api_status_t answer = answer_of(API_STATUS_HTTP_ERROR);
    config_t cfg;
    pc_status_t st;
    test_make_config(&cfg);
    snprintf(cfg.token, sizeof cfg.token, "500");
    answer.http_code = 500;
    pc_status_describe(&answer, &cfg, &st);
    CHECK_STR_EQ(st.detail, "PC error: HTTP ***");
}

static void no_state_shows_the_token_or_non_ascii_text(void)
{
    for (int kind = API_STATUS_OK; kind <= API_STATUS_HTTP_ERROR; kind++) {
        for (int running = 0; running <= 1; running++) {
            api_status_t answer = answer_of((api_status_kind_t)kind);
            pc_status_t st;
            answer.game_running = running != 0;
            answer.save_present = true;
            answer.http_code = 500;
            answer.curl_code = 56;
            snprintf(answer.save_modified, sizeof answer.save_modified, "2026-11-03 14:25");
            describe(&answer, &st);
            CHECK_STR_NOT_CONTAINS(st.status_line, CONTRACT_TOKEN);
            CHECK_STR_NOT_CONTAINS(st.detail, CONTRACT_TOKEN);
            CHECK(test_is_ascii_text(st.status_line));
            CHECK(test_is_ascii_text(st.detail));
        }
    }
}

static void refresh_reports_the_launcher_save_from_a_real_status_body(void)
{
    const scripted_reply_t script[] = {
        { 200, 0, LAUNCHER_MARKER_HEADERS,
          "{\"app\":\"totk-sync\",\"api\":1,\"game_running\":false,\"save\":{\"present\":true,\"modified\":\"2026-11-03T14:25:01+03:00\"}}" },
    };
    fake_world_t world;
    config_t cfg;
    pc_status_t st;
    fake_world_init(&world, script, 1);
    test_make_config(&cfg);
    pc_status_refresh(&world.env, &cfg, &st);
    CHECK_STR_EQ(st.status_line, "PC: online, save from 2026-11-03 14:25");
    CHECK_STR_EQ(world.last_request.url, "http://192.168.1.10:8765/status");
    CHECK_INT_EQ(world.net_calls, 1);
}

static void refresh_after_two_unreachable_attempts_says_not_reachable(void)
{
    const scripted_reply_t script[] = { { 0, 7, NULL, NULL } };
    fake_world_t world;
    config_t cfg;
    pc_status_t st;
    fake_world_init(&world, script, 1);
    test_make_config(&cfg);
    pc_status_refresh(&world.env, &cfg, &st);
    CHECK_INT_EQ(st.kind, PC_STATUS_NOT_REACHABLE);
    CHECK_INT_EQ(world.net_calls, 2);
    CHECK_INT_EQ(world.slept_seconds[0], 3);
}

static void refresh_with_a_wrong_token_does_not_retry(void)
{
    const scripted_reply_t script[] = { { 401, 0, "HTTP/1.1 401 Unauthorized\r\nX-TotkSync-Api: 1\r\n\r\n", "{\"error\":\"unauthorized\"}" } };
    fake_world_t world;
    config_t cfg;
    pc_status_t st;
    fake_world_init(&world, script, 1);
    test_make_config(&cfg);
    pc_status_refresh(&world.env, &cfg, &st);
    CHECK_STR_EQ(st.detail, "Wrong token: save switch-config.json from the PC launcher");
    CHECK_INT_EQ(world.net_calls, 1);
    CHECK_INT_EQ(world.sleep_count, 0);
}

static void refresh_of_a_foreign_web_server_says_not_a_launcher(void)
{
    const scripted_reply_t script[] = { { 200, 0, "HTTP/1.1 200 OK\r\n\r\n", "<html></html>" } };
    fake_world_t world;
    config_t cfg;
    pc_status_t st;
    fake_world_init(&world, script, 1);
    test_make_config(&cfg);
    pc_status_refresh(&world.env, &cfg, &st);
    CHECK_STR_EQ(st.detail, "Not a TotK Sync launcher at 192.168.1.10:8765");
}

void run_pc_status_tests(void)
{
    RUN_TEST(online_with_a_save_shows_its_time);
    RUN_TEST(online_without_a_save_says_no_save);
    RUN_TEST(a_running_game_wins_over_a_present_save);
    RUN_TEST(a_running_game_wins_over_a_missing_save);
    RUN_TEST(unreachable_shows_the_status_line_and_the_hint);
    RUN_TEST(wrong_token_has_the_spec_text);
    RUN_TEST(not_a_launcher_names_the_host_and_port);
    RUN_TEST(other_http_codes_are_reported_with_the_code);
    RUN_TEST(forbidden_mentions_http_403_and_the_private_network);
    RUN_TEST(a_malformed_launcher_answer_is_a_pc_error);
    RUN_TEST(other_network_failures_show_the_curl_code);
    RUN_TEST(describe_overwrites_stale_output);
    RUN_TEST(describe_removes_the_token_if_it_ever_lands_in_the_text);
    RUN_TEST(no_state_shows_the_token_or_non_ascii_text);
    RUN_TEST(refresh_reports_the_launcher_save_from_a_real_status_body);
    RUN_TEST(refresh_after_two_unreachable_attempts_says_not_reachable);
    RUN_TEST(refresh_with_a_wrong_token_does_not_retry);
    RUN_TEST(refresh_of_a_foreign_web_server_says_not_a_launcher);
}
