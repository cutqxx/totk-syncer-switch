#include "save_steps.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "account_pick.h"
#include "ascii_text.h"
#include "text.h"

static long long to_kb(int64_t bytes)
{
    return (long long)(bytes / 1024);
}

void save_steps_show(const sync_env_t *env, const char *format, ...)
{
    char line[TS_LINE_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    env->ui->show_line(env->ui->ctx, line);
}

steps_status_t save_steps_check_game(const sync_env_t *env, const char *outcome, char *message, size_t message_size)
{
    game_process_info_t info;
    memset(&info, 0, sizeof info);
    save_steps_show(env, "%s", TXT_STEP_CHECK_GAME);
    env->console->find_game_process(env->console->ctx, &info);
    switch (game_guard_check(&info)) {
    case GAME_CLEAR: return STEPS_OK;
    case GAME_RUNNING: snprintf(message, message_size, "%s", TXT_MSG_CLOSE_TOTK); return STEPS_GAME_RUNNING;
    default:
        snprintf(message, message_size, TXT_MSG_GAME_CHECK_FAILED, (unsigned)info.error_code, outcome);
        return STEPS_GAME_CHECK_FAILED;
    }
}

steps_status_t save_steps_pick_account(const sync_env_t *env, const char *title, const char *outcome, account_t *account,
                                       char *message, size_t message_size)
{
    switch (account_pick(env, title, account)) {
    case PICK_CHOSEN: return STEPS_OK;
    case PICK_NO_SAVE: snprintf(message, message_size, "%s", TXT_MSG_NO_SAVE_ON_CONSOLE); return STEPS_NO_SAVE_ON_CONSOLE;
    default: snprintf(message, message_size, TXT_MSG_CANCELLED, outcome); return STEPS_CANCELLED;
    }
}

void save_steps_describe_pack_failure(pack_status_t status, const pack_stats_t *stats, int64_t max_save_bytes,
                                      const char *outcome, char *out, size_t out_size)
{
    char shown_path[TS_PATH_SIZE];
    ascii_copy(stats->problem_path, shown_path, sizeof shown_path);
    switch (status) {
    case PACK_ERR_TOO_LARGE:
        snprintf(out, out_size, TXT_MSG_SAVE_TOO_LARGE, to_kb(stats->total_bytes), to_kb(max_save_bytes), outcome);
        break;
    case PACK_ERR_TOO_MANY_ENTRIES: snprintf(out, out_size, TXT_MSG_TOO_MANY_ENTRIES, TS_MAX_SAVE_ENTRIES, outcome); break;
    case PACK_ERR_BAD_NAME: snprintf(out, out_size, TXT_MSG_BAD_ENTRY_NAME, shown_path, outcome); break;
    case PACK_ERR_READ: snprintf(out, out_size, TXT_MSG_SAVE_READ_FAILED, shown_path, outcome); break;
    default: snprintf(out, out_size, TXT_MSG_ZIP_FAILED, outcome); break;
    }
}

steps_status_t save_steps_read_save(const sync_env_t *env, const account_t *account, int64_t max_save_bytes,
                                    const char *outcome, body_t *zip, pack_stats_t *stats, char *message,
                                    size_t message_size)
{
    const console_port_t *console = env->console;
    char root[TS_PATH_SIZE];
    uint32_t error_code = 0;
    struct tm now;
    save_steps_show(env, TXT_STEP_READING_SAVE, account->name);
    if (!console->mount_save_readonly(console->ctx, account, root, sizeof root, &error_code)) {
        snprintf(message, message_size, TXT_MSG_MOUNT_FAILED, (unsigned)error_code, outcome);
        return STEPS_MOUNT_FAILED;
    }
    env->clock->now_local(env->clock->ctx, &now);
    pack_status_t status = save_pack_dir(root, &now, max_save_bytes, zip, stats);
    console->unmount_save(console->ctx);
    if (status != PACK_OK) {
        save_steps_describe_pack_failure(status, stats, max_save_bytes, outcome, message, message_size);
        return STEPS_PACK_FAILED;
    }
    save_steps_show(env, TXT_STEP_SAVE_READ, stats->files, stats->directories, to_kb(stats->total_bytes));
    save_steps_show(env, TXT_STEP_ZIP_READY, to_kb(stats->zip_bytes));
    return STEPS_OK;
}
