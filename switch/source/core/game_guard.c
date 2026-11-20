#include "game_guard.h"

game_state_t game_guard_check(const game_process_info_t *info)
{
    if (!info->is_probe_ok) {
        return GAME_UNKNOWN;
    }
    bool is_another_process = info->is_found && info->pid != info->self_pid;
    return is_another_process ? GAME_RUNNING : GAME_CLEAR;
}
