#include <string.h>

#include "game_guard.h"
#include "test.h"

#define SELF_PID 77

static game_process_info_t probe(bool is_probe_ok, bool is_found, uint64_t pid, uint32_t error_code)
{
    game_process_info_t info;
    memset(&info, 0, sizeof info);
    info.is_probe_ok = is_probe_ok;
    info.is_found = is_found;
    info.pid = pid;
    info.self_pid = SELF_PID;
    info.error_code = error_code;
    return info;
}

static void the_game_is_clear_only_when_the_probe_worked_and_found_no_process(void)
{
    game_process_info_t info = probe(true, false, 0, 0);
    CHECK_INT_EQ(game_guard_check(&info), GAME_CLEAR);
}

static void a_found_process_means_the_game_is_running(void)
{
    game_process_info_t info = probe(true, true, 4242, 0);
    CHECK_INT_EQ(game_guard_check(&info), GAME_RUNNING);
}

static void a_suspended_process_is_found_like_a_running_one(void)
{
    game_process_info_t info = probe(true, true, 1, 0);
    CHECK_INT_EQ(game_guard_check(&info), GAME_RUNNING);
}

static void our_own_process_does_not_count_under_a_title_override(void)
{
    game_process_info_t info = probe(true, true, SELF_PID, 0);
    CHECK_INT_EQ(game_guard_check(&info), GAME_CLEAR);
}

static void a_zeroed_probe_is_unknown_not_clear(void)
{
    game_process_info_t info;
    memset(&info, 0, sizeof info);
    CHECK_INT_EQ(game_guard_check(&info), GAME_UNKNOWN);
}

static void any_failed_probe_is_unknown_whatever_else_it_says(void)
{
    static const uint32_t errors[] = { 0, 1, 0x20F, 0x215, 0x234E, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof errors / sizeof errors[0]; i++) {
        game_process_info_t not_found = probe(false, false, 0, errors[i]);
        game_process_info_t found = probe(false, true, 4242, errors[i]);
        game_process_info_t found_self = probe(false, true, SELF_PID, errors[i]);
        CHECK_INT_EQ(game_guard_check(&not_found), GAME_UNKNOWN);
        CHECK_INT_EQ(game_guard_check(&found), GAME_UNKNOWN);
        CHECK_INT_EQ(game_guard_check(&found_self), GAME_UNKNOWN);
    }
}

void run_game_guard_tests(void)
{
    RUN_TEST(the_game_is_clear_only_when_the_probe_worked_and_found_no_process);
    RUN_TEST(a_found_process_means_the_game_is_running);
    RUN_TEST(a_suspended_process_is_found_like_a_running_one);
    RUN_TEST(our_own_process_does_not_count_under_a_title_override);
    RUN_TEST(a_zeroed_probe_is_unknown_not_clear);
    RUN_TEST(any_failed_probe_is_unknown_whatever_else_it_says);
}
