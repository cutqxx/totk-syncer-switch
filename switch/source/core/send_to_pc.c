#include "send_to_pc.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "env_log.h"
#include "redact.h"
#include "retry.h"
#include "save_steps.h"
#include "text.h"

typedef struct {
    const sync_env_t *env;
    const config_t *cfg;
    int64_t max_save_bytes;
    send_result_t *result;
} send_context_t;

static void fail(send_context_t *ctx, send_kind_t kind, const char *format, ...) __attribute__((format(printf, 3, 4)));

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
    save_steps_describe_pack_failure(status, stats, max_save_bytes, TXT_NOTHING_SENT, out, out_size);
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
    case API_PUT_UNCHANGED: snprintf(out, out_size, "%s", TXT_RESULT_UNCHANGED); break;
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

void send_format_conflict_prompt(const api_put_result_t *put, char *out, size_t out_size)
{
    const char *last_sync = put->last_sync_at[0] != '\0' ? put->last_sync_at : TXT_LAST_SYNC_NEVER;
    snprintf(out, out_size, TXT_PROMPT_OVERWRITE, put->pc_modified, last_sync);
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

static send_kind_t send_kind_of(steps_status_t status)
{
    switch (status) {
    case STEPS_GAME_RUNNING: return SEND_GAME_RUNNING;
    case STEPS_GAME_CHECK_FAILED: return SEND_GAME_CHECK_FAILED;
    case STEPS_NO_SAVE_ON_CONSOLE: return SEND_NO_SAVE_ON_CONSOLE;
    case STEPS_CANCELLED: return SEND_CANCELLED;
    case STEPS_MOUNT_FAILED: return SEND_MOUNT_FAILED;
    default: return SEND_PACK_FAILED;
    }
}

static bool step_passed(send_context_t *ctx, steps_status_t status)
{
    if (status != STEPS_OK) {
        ctx->result->kind = send_kind_of(status);
    }
    return status == STEPS_OK;
}

static bool read_save_into_zip(send_context_t *ctx, body_t *zip, pack_stats_t *stats)
{
    send_result_t *result = ctx->result;
    account_t account;
    return step_passed(ctx, save_steps_check_game(ctx->env, TXT_NOTHING_READ, result->message, sizeof result->message))
           && step_passed(ctx, save_steps_pick_account(ctx->env, TXT_ACCOUNT_MENU_TITLE, TXT_NOTHING_SENT, &account,
                                                       result->message, sizeof result->message))
           && step_passed(ctx, save_steps_read_save(ctx->env, &account, ctx->max_save_bytes, TXT_NOTHING_SENT, zip, stats,
                                                    result->message, sizeof result->message));
}

static void record_put_result(send_context_t *ctx, const api_put_result_t *put)
{
    send_result_t *result = ctx->result;
    send_describe_put(put, ctx->cfg, result->message, sizeof result->message);
    if (put->kind == API_PUT_REPLACED) {
        result->kind = SEND_OK;
        snprintf(result->backup, sizeof result->backup, "%s", put->backup);
    } else if (put->kind == API_PUT_UNCHANGED) {
        result->kind = SEND_UNCHANGED;
    } else if (put->kind == API_PUT_UNREACHABLE || put->kind == API_PUT_NETWORK_ERROR) {
        result->kind = SEND_NETWORK_FAILED;
    } else {
        result->kind = SEND_PC_REFUSED;
    }
}

static void put_zip(send_context_t *ctx, const body_t *zip, const pack_stats_t *stats, bool is_forced, api_put_result_t *put)
{
    http_request_t request;
    http_response_t response;
    char note[TS_LINE_SIZE];
    if (is_forced) {
        api_build_forced_put_request(ctx->cfg, zip, &request);
    } else {
        api_build_put_request(ctx->cfg, zip, &request);
    }
    save_steps_show(ctx->env, TXT_STEP_SENDING, to_kb(zip->size));
    save_steps_show(ctx->env, "%s", TXT_STEP_WAIT);
    retry_perform_unsent(ctx->env, &request, &response);
    send_format_transfer_note(stats, &response, note, sizeof note);
    env_log_note(ctx->env, note);
    api_interpret_put(&response, put);
    http_response_free(&response);
}

static bool user_agrees_to_overwrite(send_context_t *ctx, const api_put_result_t *conflict)
{
    char prompt[TS_LINE_SIZE];
    send_format_conflict_prompt(conflict, prompt, sizeof prompt);
    return ctx->env->ui->confirm(ctx->env->ui->ctx, prompt);
}

static void upload_zip(send_context_t *ctx, const body_t *zip, const pack_stats_t *stats)
{
    api_put_result_t put;
    put_zip(ctx, zip, stats, false, &put);
    if (put.kind == API_PUT_CONFLICT) {
        if (!user_agrees_to_overwrite(ctx, &put)) {
            fail(ctx, SEND_CANCELLED, TXT_MSG_CANCELLED, TXT_NOTHING_SENT);
            return;
        }
        put_zip(ctx, zip, stats, true, &put);
    }
    record_put_result(ctx, &put);
}

void send_to_pc(const sync_env_t *env, const config_t *cfg, int64_t max_save_bytes, send_result_t *out)
{
    send_context_t ctx = { env, cfg, max_save_bytes, out };
    body_t zip = { NULL, 0 };
    pack_stats_t stats;
    memset(out, 0, sizeof *out);
    memset(&stats, 0, sizeof stats);
    if (read_save_into_zip(&ctx, &zip, &stats)) {
        upload_zip(&ctx, &zip, &stats);
    }
    body_free(&zip);
}
