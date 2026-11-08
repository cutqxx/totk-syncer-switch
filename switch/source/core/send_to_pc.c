#include "send_to_pc.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "account_pick.h"
#include "ascii_text.h"
#include "env_log.h"
#include "redact.h"
#include "retry.h"
#include "text.h"

typedef struct {
    const sync_env_t *env;
    const config_t *cfg;
    int64_t max_save_bytes;
    send_result_t *result;
} send_context_t;

static void show(const sync_env_t *env, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void fail(send_context_t *ctx, send_kind_t kind, const char *format, ...) __attribute__((format(printf, 3, 4)));

static void show(const sync_env_t *env, const char *format, ...)
{
    char line[TS_LINE_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    env->ui->show_line(env->ui->ctx, line);
}

static void fail(send_context_t *ctx, send_kind_t kind, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(ctx->result->message, sizeof ctx->result->message, format, args);
    va_end(args);
    ctx->result->kind = kind;
}

static long long to_kb(int64_t bytes)
{
    return (long long)(bytes / 1024);
}

void send_describe_pack_failure(pack_status_t status, const pack_stats_t *stats, int64_t max_save_bytes, char *out, size_t out_size)
{
    char shown_path[TS_PATH_SIZE];
    ascii_copy(stats->problem_path, shown_path, sizeof shown_path);
    switch (status) {
    case PACK_ERR_TOO_LARGE: snprintf(out, out_size, TXT_MSG_SAVE_TOO_LARGE, to_kb(stats->total_bytes), to_kb(max_save_bytes)); break;
    case PACK_ERR_TOO_MANY_ENTRIES: snprintf(out, out_size, TXT_MSG_TOO_MANY_ENTRIES, TS_MAX_SAVE_ENTRIES); break;
    case PACK_ERR_BAD_NAME: snprintf(out, out_size, TXT_MSG_BAD_ENTRY_NAME, shown_path); break;
    case PACK_ERR_READ: snprintf(out, out_size, TXT_MSG_SAVE_READ_FAILED, shown_path); break;
    default: snprintf(out, out_size, "%s", TXT_MSG_ZIP_FAILED); break;
    }
}

static void describe_network_failure(const api_put_result_t *put, char *out, size_t out_size)
{
    bool is_body_possibly_sent = put->bytes_sent > 0;
    if (is_body_possibly_sent) {
        snprintf(out, out_size, TXT_MSG_SEND_UNCERTAIN, put->curl_code);
    } else if (put->kind == API_PUT_UNREACHABLE) {
        snprintf(out, out_size, "%s", TXT_MSG_NOT_REACHABLE);
    } else {
        snprintf(out, out_size, TXT_MSG_NETWORK_OTHER, put->curl_code);
    }
}

void send_describe_put(const api_put_result_t *put, const config_t *cfg, char *out, size_t out_size)
{
    switch (put->kind) {
    case API_PUT_REPLACED: snprintf(out, out_size, TXT_RESULT_SENT, put->backup); break;
    case API_PUT_UNREACHABLE:
    case API_PUT_NETWORK_ERROR: describe_network_failure(put, out, out_size); break;
    case API_PUT_WRONG_TOKEN: snprintf(out, out_size, "%s", TXT_MSG_WRONG_TOKEN); break;
    case API_PUT_FORBIDDEN: snprintf(out, out_size, "%s", TXT_MSG_FORBIDDEN); break;
    case API_PUT_NOT_LAUNCHER: snprintf(out, out_size, TXT_MSG_NOT_LAUNCHER, cfg->pc_host, cfg->pc_port); break;
    case API_PUT_BAD_RESPONSE: snprintf(out, out_size, "%s", TXT_MSG_BAD_PUT_RESPONSE); break;
    case API_PUT_INVALID_ZIP: snprintf(out, out_size, TXT_MSG_PC_REJECTED, put->reason); break;
    case API_PUT_GAME_RUNNING: snprintf(out, out_size, "%s", TXT_MSG_PC_GAME_RUNNING); break;
    case API_PUT_BUSY: snprintf(out, out_size, "%s", TXT_MSG_PC_BUSY); break;
    case API_PUT_NO_SAVE: snprintf(out, out_size, "%s", TXT_MSG_PC_NO_SAVE); break;
    case API_PUT_WRITE_FAILED: snprintf(out, out_size, TXT_MSG_PC_WRITE_FAILED, put->reason); break;
    default: snprintf(out, out_size, TXT_MSG_HTTP_ERROR, put->http_code); break;
    }
    redact_secret_inplace(out, cfg->token);
}

void send_format_transfer_note(const pack_stats_t *stats, const http_response_t *resp, char *out, size_t out_size)
{
    double ratio = (double)stats->total_bytes / (double)(stats->zip_bytes > 0 ? stats->zip_bytes : 1);
    double kb_per_s = resp->seconds > 0 ? (double)resp->bytes_sent / 1024.0 / resp->seconds : 0.0;
    snprintf(out, out_size,
             "PUT /save: zip %" PRId64 " B, unpacked %" PRId64 " B (%d files, %d folders), compression %.1f:1, "
             "%" PRId64 " B sent in %.1f s (%.0f KB/s), HTTP %ld, curl %d",
             stats->zip_bytes, stats->total_bytes, stats->files, stats->directories, ratio, resp->bytes_sent,
             resp->seconds, kb_per_s, resp->http_code, resp->curl_code);
}

static bool confirm_game_is_closed(send_context_t *ctx)
{
    game_process_info_t info;
    memset(&info, 0, sizeof info);
    show(ctx->env, "%s", TXT_STEP_CHECK_GAME);
    ctx->env->console->find_game_process(ctx->env->console->ctx, &info);
    switch (game_guard_check(&info)) {
    case GAME_CLEAR: return true;
    case GAME_RUNNING: fail(ctx, SEND_GAME_RUNNING, "%s", TXT_MSG_CLOSE_TOTK); return false;
    default: fail(ctx, SEND_GAME_CHECK_FAILED, TXT_MSG_GAME_CHECK_FAILED, (unsigned)info.error_code); return false;
    }
}

static bool choose_account(send_context_t *ctx, account_t *account)
{
    switch (account_pick(ctx->env, account)) {
    case PICK_CHOSEN: return true;
    case PICK_NO_SAVE: fail(ctx, SEND_NO_SAVE_ON_CONSOLE, "%s", TXT_MSG_NO_SAVE_ON_CONSOLE); return false;
    default: fail(ctx, SEND_CANCELLED, "%s", TXT_MSG_SEND_CANCELLED); return false;
    }
}

static pack_status_t pack_mounted_save(send_context_t *ctx, const char *root, body_t *zip, pack_stats_t *stats)
{
    struct tm now;
    ctx->env->clock->now_local(ctx->env->clock->ctx, &now);
    return save_pack_dir(root, &now, ctx->max_save_bytes, zip, stats);
}

static void report_pack_failure(send_context_t *ctx, pack_status_t status, const pack_stats_t *stats)
{
    char message[TS_LINE_SIZE];
    send_describe_pack_failure(status, stats, ctx->max_save_bytes, message, sizeof message);
    fail(ctx, SEND_PACK_FAILED, "%s", message);
}

static bool read_save_into_zip(send_context_t *ctx, const account_t *account, body_t *zip, pack_stats_t *stats)
{
    const console_port_t *console = ctx->env->console;
    char root[TS_PATH_SIZE];
    uint32_t error_code = 0;
    show(ctx->env, TXT_STEP_READING_SAVE, account->name);
    if (!console->mount_save_readonly(console->ctx, account, root, sizeof root, &error_code)) {
        fail(ctx, SEND_MOUNT_FAILED, TXT_MSG_MOUNT_FAILED, (unsigned)error_code);
        return false;
    }
    pack_status_t status = pack_mounted_save(ctx, root, zip, stats);
    console->unmount_save(console->ctx);
    if (status != PACK_OK) {
        report_pack_failure(ctx, status, stats);
        return false;
    }
    show(ctx->env, TXT_STEP_SAVE_READ, stats->files, stats->directories, to_kb(stats->total_bytes));
    show(ctx->env, TXT_STEP_ZIP_READY, to_kb(stats->zip_bytes));
    return true;
}

static void record_put_result(send_context_t *ctx, const api_put_result_t *put)
{
    send_result_t *result = ctx->result;
    send_describe_put(put, ctx->cfg, result->message, sizeof result->message);
    if (put->kind == API_PUT_REPLACED) {
        result->kind = SEND_OK;
        snprintf(result->backup, sizeof result->backup, "%s", put->backup);
    } else if (put->kind == API_PUT_UNREACHABLE || put->kind == API_PUT_NETWORK_ERROR) {
        result->kind = SEND_NETWORK_FAILED;
    } else {
        result->kind = SEND_PC_REFUSED;
    }
}

static void upload_zip(send_context_t *ctx, const body_t *zip, const pack_stats_t *stats)
{
    http_request_t request;
    http_response_t response;
    api_put_result_t put;
    char note[TS_LINE_SIZE];
    api_build_put_request(ctx->cfg, zip, &request);
    show(ctx->env, TXT_STEP_SENDING, to_kb(zip->size));
    show(ctx->env, "%s", TXT_STEP_WAIT);
    retry_perform_unsent(ctx->env, &request, &response);
    send_format_transfer_note(stats, &response, note, sizeof note);
    env_log_note(ctx->env, note);
    api_interpret_put(&response, &put);
    http_response_free(&response);
    record_put_result(ctx, &put);
}

void send_to_pc(const sync_env_t *env, const config_t *cfg, int64_t max_save_bytes, send_result_t *out)
{
    send_context_t ctx = { env, cfg, max_save_bytes, out };
    account_t account;
    body_t zip = { NULL, 0 };
    pack_stats_t stats;
    memset(out, 0, sizeof *out);
    memset(&stats, 0, sizeof stats);
    if (confirm_game_is_closed(&ctx) && choose_account(&ctx, &account) && read_save_into_zip(&ctx, &account, &zip, &stats)) {
        upload_zip(&ctx, &zip, &stats);
    }
    body_free(&zip);
}
