#include "get_from_pc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "api_save.h"
#include "backup_to_sd.h"
#include "env_log.h"
#include "paths.h"
#include "redact.h"
#include "retry.h"
#include "save_steps.h"
#include "save_validate.h"
#include "save_write.h"
#include "text.h"
#include "zip_reader.h"

typedef struct {
    const sync_env_t *env;
    const config_t *cfg;
    pull_result_t *out;
    account_t account;
    body_t zip;
    zip_reader_t *reader;
    save_zip_stats_t stats;
    api_get_result_t answer;
} pull_t;

static bool fail(pull_t *p, pull_kind_t kind, const char *format, ...) __attribute__((format(printf, 3, 4)));

static bool fail(pull_t *p, pull_kind_t kind, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(p->out->message, sizeof p->out->message, format, args);
    va_end(args);
    p->out->kind = kind;
    return false;
}

static void log_note(const pull_t *p, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void log_note(const pull_t *p, const char *format, ...)
{
    char note[TS_LINE_SIZE + TS_PATH_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(note, sizeof note, format, args);
    va_end(args);
    env_log_note(p->env, note);
}

static long long to_kb(int64_t bytes)
{
    return (long long)((bytes + 1023) / 1024);
}

static pull_kind_t kind_of(steps_status_t status)
{
    switch (status) {
    case STEPS_GAME_RUNNING: return PULL_GAME_RUNNING;
    case STEPS_GAME_CHECK_FAILED: return PULL_GAME_CHECK_FAILED;
    case STEPS_NO_SAVE_ON_CONSOLE: return PULL_NO_SAVE_ON_CONSOLE;
    case STEPS_CANCELLED: return PULL_CANCELLED;
    default: return PULL_BACKUP_FAILED;
    }
}

static bool step_passed(pull_t *p, steps_status_t status)
{
    if (status != STEPS_OK) {
        p->out->kind = kind_of(status);
    }
    return status == STEPS_OK;
}

static bool game_is_closed(pull_t *p)
{
    return step_passed(p, save_steps_check_game(p->env, TXT_SWITCH_SAVE_NOT_CHANGED, p->out->message,
                                                sizeof p->out->message));
}

/* --- download and check -------------------------------------------------------------------------------------- */

static void describe_download_failure(const pull_t *p, char *out, size_t out_size)
{
    const api_get_result_t *a = &p->answer;
    switch (a->kind) {
    case API_GET_UNREACHABLE: snprintf(out, out_size, "%s", TXT_MSG_NOT_REACHABLE); break;
    case API_GET_NETWORK_ERROR: snprintf(out, out_size, TXT_MSG_NETWORK_OTHER, a->curl_code); break;
    case API_GET_WRONG_TOKEN: snprintf(out, out_size, "%s", TXT_MSG_WRONG_TOKEN); break;
    case API_GET_FORBIDDEN: snprintf(out, out_size, "%s", TXT_MSG_FORBIDDEN); break;
    case API_GET_NOT_LAUNCHER: snprintf(out, out_size, TXT_MSG_NOT_LAUNCHER, p->cfg->pc_host, p->cfg->pc_port); break;
    case API_GET_BAD_RESPONSE: snprintf(out, out_size, "%s", TXT_MSG_BAD_GET_RESPONSE); break;
    case API_GET_GAME_RUNNING: snprintf(out, out_size, "%s", TXT_MSG_PC_GAME_RUNNING); break;
    case API_GET_BUSY: snprintf(out, out_size, "%s", TXT_MSG_PC_BUSY); break;
    default: snprintf(out, out_size, TXT_MSG_HTTP_ERROR, a->http_code); break;
    }
}

static bool download(pull_t *p)
{
    http_request_t request;
    http_response_t response;
    save_steps_show(p->env, "%s", TXT_STEP_DOWNLOADING);
    api_build_get_save_request(p->cfg, &request);
    retry_perform(p->env, &request, &response);
    api_interpret_get_save(&response, &p->answer);
    log_note(p, "GET /save: %lld B in %.1f s, HTTP %ld, curl %d", (long long)response.body.size, response.seconds,
             response.http_code, response.curl_code);
    if (p->answer.kind == API_GET_OK) {
        p->zip = response.body;
        response.body = (body_t){ NULL, 0 };
    }
    http_response_free(&response);
    switch (p->answer.kind) {
    case API_GET_OK: return true;
    case API_GET_NO_SAVE: return fail(p, PULL_DOWNLOAD_FAILED, "%s", TXT_MSG_PULL_PC_NO_SAVE);
    case API_GET_READ_FAILED: return fail(p, PULL_DOWNLOAD_FAILED, TXT_MSG_PC_READ_FAILED, p->answer.reason);
    default: {
        char reason[TS_LINE_SIZE];
        describe_download_failure(p, reason, sizeof reason);
        redact_secret_inplace(reason, p->cfg->token);
        return fail(p, PULL_DOWNLOAD_FAILED, TXT_MSG_DOWNLOAD_FAILED, reason);
    }
    }
}

static bool check_zip(pull_t *p)
{
    char reason[SAVE_VALIDATE_REASON_SIZE];
    save_steps_show(p->env, TXT_STEP_CHECKING_ZIP, to_kb(p->zip.size));
    p->reader = zip_reader_open(p->zip.data, (size_t)p->zip.size);
    if (!save_validate_zip(p->reader, &p->stats, reason, sizeof reason)) {
        log_note(p, "PC save rejected: %s", reason);
        return fail(p, PULL_INVALID_ZIP, TXT_MSG_PC_SAVE_INVALID, reason);
    }
    return true;
}

static bool user_wants_restore(pull_t *p)
{
    char prompt[TS_LINE_SIZE];
    snprintf(prompt, sizeof prompt, TXT_PROMPT_RESTORE, p->answer.modified, p->stats.files, to_kb(p->stats.total_bytes));
    if (!p->env->ui->confirm(p->env->ui->ctx, prompt)) {
        return fail(p, PULL_CANCELLED, TXT_MSG_CANCELLED, TXT_SWITCH_SAVE_NOT_CHANGED);
    }
    return true;
}

/* --- before any change ---------------------------------------------------------------------------------------- */

static bool fits_into_the_save(pull_t *p, int64_t *journal_bytes)
{
    int64_t data_bytes = 0;
    *journal_bytes = 0;
    const console_port_t *console = p->env->console;
    if (!console->read_save_space(console->ctx, &p->account, &data_bytes, journal_bytes)) {
        log_note(p, "Switch save size unknown: writing without the size check, committing every %lld KB",
                 to_kb(SAVE_WRITE_FALLBACK_BUDGET));
        *journal_bytes = 0;
        return true;
    }
    log_note(p, "Switch save: %lld B usable, journal %lld B; PC save %lld B", (long long)data_bytes,
             (long long)*journal_bytes, (long long)p->stats.total_bytes);
    if (p->stats.total_bytes > data_bytes) {
        return fail(p, PULL_DOES_NOT_FIT, TXT_MSG_DOES_NOT_FIT, to_kb(p->stats.total_bytes), to_kb(data_bytes));
    }
    return true;
}

static bool back_up_before_pull(pull_t *p)
{
    backup_kind_t kind = backup_account_before_pull(p->env, p->cfg, &p->account, TS_MAX_SAVE_BYTES, p->out->backup,
                                                    p->out->message, sizeof p->out->message);
    if (kind != BACKUP_OK) {
        p->out->kind = PULL_BACKUP_FAILED;
        p->out->backup[0] = '\0';
        return false;
    }
    return true;
}

/* --- writing ---------------------------------------------------------------------------------------------------- */

static bool write_into_save(pull_t *p, zip_reader_t *reader, int64_t journal_bytes, save_write_result_t *written,
                            uint32_t *mount_error)
{
    const console_port_t *console = p->env->console;
    char root[TS_PATH_SIZE];
    if (!console->mount_save_readwrite(console->ctx, &p->account, root, sizeof root, mount_error)) {
        memset(written, 0, sizeof *written);
        return false;
    }
    save_write_zip(p->env->files, console, root, reader, journal_bytes, written);
    console->unmount_save(console->ctx);
    log_note(p, "Save write: %s, %d commits%s%s", written->is_ok ? "ok" : "FAILED", written->commits,
             written->is_ok ? "" : ": ", written->reason);
    return true;
}

static bool restore_backup(pull_t *p, int64_t journal_bytes, char *reason, size_t reason_size)
{
    body_t backup = { NULL, 0 };
    save_zip_stats_t stats;
    save_write_result_t written;
    uint32_t mount_error = 0;
    save_steps_show(p->env, TXT_STEP_RESTORING_BACKUP, p->out->backup);
    if (!sd_backup_read(p->env->sd_root, p->out->backup, &backup)) {
        snprintf(reason, reason_size, "cannot read the backup from the SD");
        return false;
    }
    zip_reader_t *reader = zip_reader_open(backup.data, (size_t)backup.size);
    bool is_ok = save_validate_zip(reader, &stats, reason, reason_size);
    if (is_ok && !write_into_save(p, reader, journal_bytes, &written, &mount_error)) {
        snprintf(reason, reason_size, "cannot open the TotK save for writing (error 0x%x)", (unsigned)mount_error);
        is_ok = false;
    } else if (is_ok && !written.is_ok) {
        snprintf(reason, reason_size, "%s", written.reason);
        is_ok = false;
    }
    zip_reader_close(reader);
    body_free(&backup);
    return is_ok;
}

static void offer_restore(pull_t *p, int64_t journal_bytes, const char *write_reason)
{
    char prompt[2 * TS_LINE_SIZE];
    char backup_path[TS_PATH_SIZE];
    char rel[TS_PATH_SIZE];
    char reason[TS_LINE_SIZE];
    snprintf(rel, sizeof rel, "%s/%s", TS_REL_BACKUP_DIR, p->out->backup);
    paths_display(rel, backup_path, sizeof backup_path);
    snprintf(prompt, sizeof prompt, TXT_PROMPT_WRITE_FAILED, write_reason, p->out->backup);
    if (!p->env->ui->confirm(p->env->ui->ctx, prompt)) {
        fail(p, PULL_LEFT_INCOMPLETE, TXT_MSG_LEFT_INCOMPLETE, write_reason, backup_path);
        return;
    }
    if (restore_backup(p, journal_bytes, reason, sizeof reason)) {
        log_note(p, "Backup %s restored after a failed write", p->out->backup);
        fail(p, PULL_RESTORED_BACKUP, TXT_RESULT_BACKUP_RESTORED, p->out->backup);
        return;
    }
    log_note(p, "Restore of %s failed: %s", p->out->backup, reason);
    fail(p, PULL_RESTORE_FAILED, TXT_MSG_RESTORE_FAILED, reason, backup_path);
}

static void confirm_to_pc(pull_t *p)
{
    http_request_t request;
    http_response_t response;
    body_t ack = { NULL, 0 };
    bool is_recorded = false;
    if (api_build_ack_request(p->cfg, p->answer.fingerprint, &ack, &request)) {
        retry_perform(p->env, &request, &response);
        is_recorded = api_interpret_ack(&response);
        http_response_free(&response);
    }
    body_free(&ack);
    p->out->kind = is_recorded ? PULL_OK : PULL_OK_ACK_FAILED;
    snprintf(p->out->message, sizeof p->out->message, TXT_RESULT_RESTORED "%s%s", p->out->backup,
             is_recorded ? "" : "\n", is_recorded ? "" : TXT_WARN_ACK_FAILED);
}

static void apply(pull_t *p, int64_t journal_bytes)
{
    const console_port_t *console = p->env->console;
    save_write_result_t written;
    uint32_t mount_error = 0;
    save_steps_show(p->env, "%s", TXT_STEP_WRITING_SAVE);
    console->set_home_blocked(console->ctx, true);
    if (!write_into_save(p, p->reader, journal_bytes, &written, &mount_error)) {
        fail(p, PULL_MOUNT_FAILED, TXT_MSG_RW_MOUNT_FAILED, (unsigned)mount_error);
    } else if (!written.is_ok && !written.is_changed) {
        fail(p, PULL_WRITE_FAILED, TXT_MSG_WRITE_FAILED_UNCHANGED, written.reason);
    } else if (!written.is_ok) {
        offer_restore(p, journal_bytes, written.reason);
    }
    console->set_home_blocked(console->ctx, false);
    if (written.is_ok) {
        confirm_to_pc(p);
    }
}

void get_from_pc(const sync_env_t *env, const config_t *cfg, pull_result_t *out)
{
    pull_t p;
    int64_t journal_bytes = 0;
    memset(&p, 0, sizeof p);
    memset(out, 0, sizeof *out);
    p.env = env;
    p.cfg = cfg;
    p.out = out;
    bool is_ready = game_is_closed(&p)
                    && step_passed(&p, save_steps_pick_account(env, TXT_ACCOUNT_MENU_PULL_TITLE, TXT_SWITCH_SAVE_NOT_CHANGED,
                                                               &p.account, out->message, sizeof out->message))
                    && download(&p) && check_zip(&p) && user_wants_restore(&p) && game_is_closed(&p)
                    && fits_into_the_save(&p, &journal_bytes) && back_up_before_pull(&p) && game_is_closed(&p);
    if (is_ready) {
        apply(&p, journal_bytes);
    }
    zip_reader_close(p.reader);
    body_free(&p.zip);
}
