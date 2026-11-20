#include "backup_to_sd.h"

#include <stdio.h>
#include <string.h>

#include "env_log.h"
#include "save_steps.h"
#include "text.h"

static backup_kind_t kind_of(steps_status_t status)
{
    switch (status) {
    case STEPS_OK: return BACKUP_OK;
    case STEPS_GAME_RUNNING: return BACKUP_GAME_RUNNING;
    case STEPS_GAME_CHECK_FAILED: return BACKUP_GAME_CHECK_FAILED;
    case STEPS_NO_SAVE_ON_CONSOLE: return BACKUP_NO_SAVE_ON_CONSOLE;
    case STEPS_CANCELLED: return BACKUP_CANCELLED;
    case STEPS_MOUNT_FAILED: return BACKUP_MOUNT_FAILED;
    default: return BACKUP_PACK_FAILED;
    }
}

static backup_kind_t write_to_sd(const sync_env_t *env, const config_t *cfg, sd_backup_reason_t reason, const body_t *zip,
                                 char *name_out, char *message, size_t message_size, const char *failure_format,
                                 const char *outcome)
{
    struct tm now;
    sd_backup_result_t written;
    char note[TS_LINE_SIZE + TS_PATH_SIZE];
    env->clock->now_local(env->clock->ctx, &now);
    save_steps_show(env, TXT_STEP_WRITING_BACKUP, (long long)(zip->size / 1024));
    if (!sd_backup_write(env->files, env->sd_root, &now, reason, zip, cfg->keep_backups, &written)) {
        snprintf(message, message_size, failure_format, written.error, outcome);
        snprintf(note, sizeof note, "SD backup failed: %s", written.error);
        env_log_note(env, note);
        return BACKUP_SD_FAILED;
    }
    snprintf(name_out, SD_BACKUP_NAME_SIZE, "%s", written.name);
    snprintf(message, message_size, TXT_RESULT_BACKUP_OK, written.display_path);
    snprintf(note, sizeof note, "SD backup %s: %lld B, %d old backups pruned", written.display_path, (long long)zip->size,
             written.pruned);
    env_log_note(env, note);
    return BACKUP_OK;
}

void backup_save_to_sd(const sync_env_t *env, const config_t *cfg, int64_t max_save_bytes, backup_result_t *out)
{
    account_t account;
    body_t zip = { NULL, 0 };
    pack_stats_t stats;
    memset(out, 0, sizeof *out);
    memset(&stats, 0, sizeof stats);
    steps_status_t status = save_steps_check_game(env, TXT_NO_BACKUP_MADE, out->message, sizeof out->message);
    if (status == STEPS_OK) {
        status = save_steps_pick_account(env, TXT_ACCOUNT_MENU_BACKUP_TITLE, TXT_NO_BACKUP_MADE, &account, out->message,
                                         sizeof out->message);
    }
    if (status == STEPS_OK) {
        status = save_steps_read_save(env, &account, max_save_bytes, TXT_NO_BACKUP_MADE, &zip, &stats, out->message,
                                      sizeof out->message);
    }
    if (status != STEPS_OK) {
        out->kind = kind_of(status);
    } else {
        out->kind = write_to_sd(env, cfg, SD_BACKUP_MANUAL, &zip, out->name, out->message, sizeof out->message,
                                TXT_MSG_BACKUP_FAILED, TXT_NO_BACKUP_MADE);
    }
    body_free(&zip);
}

backup_kind_t backup_account_before_pull(const sync_env_t *env, const config_t *cfg, const account_t *account,
                                         int64_t max_save_bytes, char *name_out, char *message, size_t message_size)
{
    body_t zip = { NULL, 0 };
    pack_stats_t stats;
    memset(&stats, 0, sizeof stats);
    steps_status_t status = save_steps_read_save(env, account, max_save_bytes, TXT_SWITCH_SAVE_NOT_CHANGED, &zip, &stats,
                                                 message, message_size);
    backup_kind_t kind = kind_of(status);
    if (status == STEPS_OK) {
        kind = write_to_sd(env, cfg, SD_BACKUP_BEFORE_PULL, &zip, name_out, message, message_size,
                           TXT_MSG_PRE_PULL_BACKUP_FAILED, TXT_SWITCH_SAVE_NOT_CHANGED);
    }
    body_free(&zip);
    return kind;
}
