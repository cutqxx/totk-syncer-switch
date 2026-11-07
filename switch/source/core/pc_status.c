#include "pc_status.h"

#include <stdio.h>
#include <string.h>

#include "redact.h"
#include "retry.h"
#include "text.h"

static void set_online(const api_status_t *answer, pc_status_t *out)
{
    out->kind = PC_STATUS_ONLINE;
    if (answer->game_running) {
        snprintf(out->status_line, sizeof out->status_line, "%s", TXT_STATUS_GAME_RUNNING);
    } else if (answer->save_present) {
        snprintf(out->status_line, sizeof out->status_line, TXT_STATUS_ONLINE_SAVE, answer->save_modified);
    } else {
        snprintf(out->status_line, sizeof out->status_line, "%s", TXT_STATUS_ONLINE_NO_SAVE);
    }
}

static void set_error(pc_status_t *out, const char *status_line)
{
    out->kind = PC_STATUS_ERROR;
    snprintf(out->status_line, sizeof out->status_line, "%s", status_line);
}

static void set_error_detail(const api_status_t *answer, const config_t *cfg, pc_status_t *out)
{
    switch (answer->kind) {
    case API_STATUS_WRONG_TOKEN: snprintf(out->detail, sizeof out->detail, "%s", TXT_MSG_WRONG_TOKEN); break;
    case API_STATUS_FORBIDDEN: snprintf(out->detail, sizeof out->detail, "%s", TXT_MSG_FORBIDDEN); break;
    case API_STATUS_NOT_LAUNCHER: snprintf(out->detail, sizeof out->detail, TXT_MSG_NOT_LAUNCHER, cfg->pc_host, cfg->pc_port); break;
    case API_STATUS_BAD_RESPONSE: snprintf(out->detail, sizeof out->detail, "%s", TXT_MSG_BAD_RESPONSE); break;
    case API_STATUS_HTTP_ERROR: snprintf(out->detail, sizeof out->detail, TXT_MSG_HTTP_ERROR, answer->http_code); break;
    default: snprintf(out->detail, sizeof out->detail, TXT_MSG_NETWORK_OTHER, answer->curl_code); break;
    }
}

static void set_not_reachable(pc_status_t *out)
{
    out->kind = PC_STATUS_NOT_REACHABLE;
    snprintf(out->status_line, sizeof out->status_line, "%s", TXT_STATUS_NOT_REACHABLE);
    snprintf(out->detail, sizeof out->detail, "%s", TXT_MSG_NOT_REACHABLE);
}

void pc_status_describe(const api_status_t *answer, const config_t *cfg, pc_status_t *out)
{
    memset(out, 0, sizeof *out);
    if (answer->kind == API_STATUS_OK) {
        set_online(answer, out);
    } else if (answer->kind == API_STATUS_UNREACHABLE) {
        set_not_reachable(out);
    } else {
        set_error(out, TXT_STATUS_ERROR);
        set_error_detail(answer, cfg, out);
    }
    redact_secret_inplace(out->status_line, cfg->token);
    redact_secret_inplace(out->detail, cfg->token);
}

void pc_status_refresh(const sync_env_t *env, const config_t *cfg, pc_status_t *out)
{
    http_request_t request;
    http_response_t response;
    api_status_t answer;
    api_build_status_request(cfg, &request);
    retry_perform(env, &request, &response);
    api_interpret_status(&response, &answer);
    http_response_free(&response);
    pc_status_describe(&answer, cfg, out);
}
