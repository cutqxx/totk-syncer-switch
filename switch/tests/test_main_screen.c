#include <stdio.h>
#include <string.h>

#include "main_screen.h"
#include "test.h"
#include "test_support.h"

#define BLOCKING_ERROR "Field \"pc_port\" must be a number from 1024 to 65535 in sd:/config/totk-sync/config.json"

static pc_status_t online_status(void)
{
    pc_status_t st;
    memset(&st, 0, sizeof st);
    st.kind = PC_STATUS_ONLINE;
    snprintf(st.status_line, sizeof st.status_line, "PC: online, save from 2026-11-03 14:25");
    return st;
}

static void config_error_screen_shows_the_error_with_its_path_and_only_the_exit_hint(void)
{
    char screen[MAIN_SCREEN_SIZE];
    main_screen_render(NULL, BLOCKING_ERROR, NULL, screen, sizeof screen);
    CHECK(strstr(screen, BLOCKING_ERROR) != NULL);
    CHECK(strstr(screen, "sd:/config/totk-sync/config.json") != NULL);
    CHECK(strstr(screen, "+ - exit") != NULL);
}

static void config_error_screen_offers_no_network_actions(void)
{
    char screen[MAIN_SCREEN_SIZE];
    main_screen_render(NULL, BLOCKING_ERROR, NULL, screen, sizeof screen);
    CHECK_STR_NOT_CONTAINS(screen, "A - Send to PC");
    CHECK_STR_NOT_CONTAINS(screen, "R - refresh");
    CHECK_STR_NOT_CONTAINS(screen, "http://");
    CHECK_STR_NOT_CONTAINS(screen, "PC: ");
}

static void ready_screen_shows_config_path_and_pc_address(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    test_make_config(&cfg);
    main_screen_render(&cfg, NULL, NULL, screen, sizeof screen);
    CHECK(strstr(screen, "Config:  sd:/config/totk-sync/config.json") != NULL);
    CHECK(strstr(screen, "http://192.168.1.10:8765") != NULL);
}

static void ready_screen_shows_the_three_button_hints(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    test_make_config(&cfg);
    main_screen_render(&cfg, NULL, NULL, screen, sizeof screen);
    CHECK(strstr(screen, "A - Send to PC") != NULL);
    CHECK(strstr(screen, "R - refresh") != NULL);
    CHECK(strstr(screen, "+ - exit") != NULL);
}

static void screen_says_checking_until_the_status_arrives(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    test_make_config(&cfg);
    main_screen_render(&cfg, NULL, NULL, screen, sizeof screen);
    CHECK(strstr(screen, "PC: checking...") != NULL);
}

static void screen_shows_the_status_line_and_no_checking_text_once_known(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    pc_status_t st = online_status();
    test_make_config(&cfg);
    main_screen_render(&cfg, NULL, &st, screen, sizeof screen);
    CHECK(strstr(screen, "PC: online, save from 2026-11-03 14:25") != NULL);
    CHECK_STR_NOT_CONTAINS(screen, "checking");
}

static void screen_shows_the_error_detail_under_the_status_line(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    pc_status_t st;
    test_make_config(&cfg);
    memset(&st, 0, sizeof st);
    st.kind = PC_STATUS_ERROR;
    snprintf(st.status_line, sizeof st.status_line, "PC: error");
    snprintf(st.detail, sizeof st.detail, "Wrong token: save switch-config.json from the PC launcher");
    main_screen_render(&cfg, NULL, &st, screen, sizeof screen);
    CHECK(strstr(screen, "PC: error\nWrong token: save switch-config.json from the PC launcher\n") != NULL);
}

static void screen_never_contains_the_token_in_any_state(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    pc_status_t online = online_status();
    pc_status_t failed;
    test_make_config(&cfg);
    memset(&failed, 0, sizeof failed);
    snprintf(failed.status_line, sizeof failed.status_line, "PC: error");
    snprintf(failed.detail, sizeof failed.detail, "PC error: HTTP 500");

    main_screen_render(&cfg, NULL, NULL, screen, sizeof screen);
    CHECK_STR_NOT_CONTAINS(screen, CONTRACT_TOKEN);
    main_screen_render(&cfg, NULL, &online, screen, sizeof screen);
    CHECK_STR_NOT_CONTAINS(screen, CONTRACT_TOKEN);
    main_screen_render(&cfg, NULL, &failed, screen, sizeof screen);
    CHECK_STR_NOT_CONTAINS(screen, CONTRACT_TOKEN);
    main_screen_render(NULL, BLOCKING_ERROR, NULL, screen, sizeof screen);
    CHECK_STR_NOT_CONTAINS(screen, CONTRACT_TOKEN);
    CHECK_STR_NOT_CONTAINS(screen, "token:");
}

static void screen_is_plain_ascii_in_every_state(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    pc_status_t online = online_status();
    test_make_config(&cfg);
    main_screen_render(&cfg, NULL, NULL, screen, sizeof screen);
    CHECK(test_is_ascii_text(screen));
    main_screen_render(&cfg, NULL, &online, screen, sizeof screen);
    CHECK(test_is_ascii_text(screen));
    main_screen_render(NULL, BLOCKING_ERROR, NULL, screen, sizeof screen);
    CHECK(test_is_ascii_text(screen));
}

static void worst_case_content_still_fits_the_screen_buffer_with_the_hints(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    pc_status_t st;
    test_make_config(&cfg);
    memset(cfg.source_display_path, 'p', sizeof cfg.source_display_path - 1);
    cfg.source_display_path[sizeof cfg.source_display_path - 1] = '\0';
    snprintf(cfg.pc_host, sizeof cfg.pc_host, "192.168.100.100");
    cfg.pc_port = 65535;
    memset(&st, 0, sizeof st);
    memset(st.status_line, 's', sizeof st.status_line - 1);
    memset(st.detail, 'd', sizeof st.detail - 1);
    main_screen_render(&cfg, NULL, &st, screen, sizeof screen);
    CHECK(strstr(screen, "A - Send to PC    X - Get from PC    Y - Backup to SD\nR - refresh    + - exit") != NULL);
}

static void a_small_output_buffer_is_never_overrun_and_stays_terminated(void)
{
    config_t cfg;
    pc_status_t st = online_status();
    test_make_config(&cfg);
    for (size_t size = 1; size < 300; size++) {
        char guarded[MAIN_SCREEN_SIZE + 16];
        memset(guarded, 0x5A, sizeof guarded);
        main_screen_render(&cfg, NULL, &st, guarded, size);
        CHECK(memchr(guarded, '\0', size) != NULL);
        CHECK_INT_EQ((unsigned char)guarded[size], 0x5A);
        CHECK_INT_EQ((unsigned char)guarded[size + 8], 0x5A);
    }
}

static void a_small_output_buffer_is_safe_for_the_error_screen_too(void)
{
    for (size_t size = 1; size < 200; size++) {
        char guarded[MAIN_SCREEN_SIZE + 16];
        memset(guarded, 0x5A, sizeof guarded);
        main_screen_render(NULL, BLOCKING_ERROR, NULL, guarded, size);
        CHECK(memchr(guarded, '\0', size) != NULL);
        CHECK_INT_EQ((unsigned char)guarded[size], 0x5A);
    }
}

static void a_zero_size_buffer_is_left_untouched(void)
{
    char guarded[4] = { 'a', 'b', 'c', 'd' };
    main_screen_render(NULL, BLOCKING_ERROR, NULL, guarded, 0);
    CHECK(memcmp(guarded, "abcd", 4) == 0);
}

#define SENT_RESULT "SENT TO PC. PC backup: pc-20261103-142501-before-switch.zip"

static void the_result_block_sits_between_the_status_and_the_button_hints(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    pc_status_t st = online_status();
    test_make_config(&cfg);
    main_screen_render_with_result(&cfg, &st, SENT_RESULT, screen, sizeof screen);
    CHECK_STR_EQ(screen,
                 "TotK Sync - connect to PC\n\n"
                 "Config:  sd:/config/totk-sync/config.json\n"
                 "PC:      http://192.168.1.10:8765\n\n"
                 "PC: online, save from 2026-11-03 14:25\n"
                 "\n" SENT_RESULT "\n"
                 "\nA - Send to PC    X - Get from PC    Y - Backup to SD\nR - refresh    + - exit\n");
}

static void without_a_result_the_screen_equals_the_plain_connected_screen(void)
{
    char with_result[MAIN_SCREEN_SIZE];
    char plain[MAIN_SCREEN_SIZE];
    config_t cfg;
    pc_status_t st = online_status();
    test_make_config(&cfg);
    main_screen_render_with_result(&cfg, &st, NULL, with_result, sizeof with_result);
    main_screen_render(&cfg, NULL, &st, plain, sizeof plain);
    CHECK_STR_EQ(with_result, plain);
}

static void a_multi_line_result_is_shown_whole(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    test_make_config(&cfg);
    main_screen_render_with_result(&cfg, NULL, "line one\nline two", screen, sizeof screen);
    CHECK(strstr(screen, "\nline one\nline two\n\nA - Send to PC") != NULL);
    CHECK(strstr(screen, "PC: checking...") != NULL);
}

static void the_screen_with_a_result_never_contains_the_token_and_is_ascii(void)
{
    char screen[MAIN_SCREEN_SIZE];
    config_t cfg;
    pc_status_t st = online_status();
    test_make_config(&cfg);
    main_screen_render_with_result(&cfg, &st, SENT_RESULT, screen, sizeof screen);
    CHECK_STR_NOT_CONTAINS(screen, CONTRACT_TOKEN);
    CHECK_STR_NOT_CONTAINS(screen, "token");
    CHECK(test_is_ascii_text(screen));
}

static void the_worst_case_result_still_leaves_the_button_hints_in_the_buffer(void)
{
    char screen[MAIN_SCREEN_SIZE];
    char result[TS_LINE_SIZE];
    config_t cfg;
    pc_status_t st;
    test_make_config(&cfg);
    memset(cfg.source_display_path, 'p', sizeof cfg.source_display_path - 1);
    cfg.source_display_path[sizeof cfg.source_display_path - 1] = '\0';
    snprintf(cfg.pc_host, sizeof cfg.pc_host, "192.168.100.100");
    cfg.pc_port = 65535;
    memset(&st, 0, sizeof st);
    memset(st.status_line, 's', sizeof st.status_line - 1);
    memset(st.detail, 'd', sizeof st.detail - 1);
    memset(result, 'r', sizeof result - 1);
    result[sizeof result - 1] = '\0';
    main_screen_render_with_result(&cfg, &st, result, screen, sizeof screen);
    CHECK(strstr(screen, result) != NULL);
    CHECK(strstr(screen, "A - Send to PC    X - Get from PC    Y - Backup to SD\nR - refresh    + - exit\n") != NULL);
}

static void a_small_buffer_is_never_overrun_when_a_result_is_shown(void)
{
    config_t cfg;
    pc_status_t st = online_status();
    test_make_config(&cfg);
    for (size_t size = 1; size < MAIN_SCREEN_SIZE + 8; size++) {
        char guarded[MAIN_SCREEN_SIZE + 16];
        memset(guarded, 0x5A, sizeof guarded);
        main_screen_render_with_result(&cfg, &st, SENT_RESULT, guarded, size);
        CHECK(memchr(guarded, '\0', size) != NULL);
        CHECK_INT_EQ((unsigned char)guarded[size], 0x5A);
        CHECK_INT_EQ((unsigned char)guarded[size + 8], 0x5A);
    }
}

static void a_zero_size_buffer_is_left_untouched_when_a_result_is_shown(void)
{
    char guarded[4] = { 'a', 'b', 'c', 'd' };
    config_t cfg;
    test_make_config(&cfg);
    main_screen_render_with_result(&cfg, NULL, SENT_RESULT, guarded, 0);
    CHECK(memcmp(guarded, "abcd", 4) == 0);
}

void run_main_screen_tests(void)
{
    RUN_TEST(config_error_screen_shows_the_error_with_its_path_and_only_the_exit_hint);
    RUN_TEST(config_error_screen_offers_no_network_actions);
    RUN_TEST(ready_screen_shows_config_path_and_pc_address);
    RUN_TEST(ready_screen_shows_the_three_button_hints);
    RUN_TEST(screen_says_checking_until_the_status_arrives);
    RUN_TEST(screen_shows_the_status_line_and_no_checking_text_once_known);
    RUN_TEST(screen_shows_the_error_detail_under_the_status_line);
    RUN_TEST(screen_never_contains_the_token_in_any_state);
    RUN_TEST(screen_is_plain_ascii_in_every_state);
    RUN_TEST(worst_case_content_still_fits_the_screen_buffer_with_the_hints);
    RUN_TEST(a_small_output_buffer_is_never_overrun_and_stays_terminated);
    RUN_TEST(a_small_output_buffer_is_safe_for_the_error_screen_too);
    RUN_TEST(a_zero_size_buffer_is_left_untouched);
    RUN_TEST(the_result_block_sits_between_the_status_and_the_button_hints);
    RUN_TEST(without_a_result_the_screen_equals_the_plain_connected_screen);
    RUN_TEST(a_multi_line_result_is_shown_whole);
    RUN_TEST(the_screen_with_a_result_never_contains_the_token_and_is_ascii);
    RUN_TEST(the_worst_case_result_still_leaves_the_button_hints_in_the_buffer);
    RUN_TEST(a_small_buffer_is_never_overrun_when_a_result_is_shown);
    RUN_TEST(a_zero_size_buffer_is_left_untouched_when_a_result_is_shown);
}
