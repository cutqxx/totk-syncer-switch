#include "retry.h"

#include <stdio.h>

#include "outcome.h"
#include "text.h"

static void log_attempt(const sync_env_t *env, const http_request_t *req, const http_response_t *resp, int attempt)
{
    if (env->log == NULL) {
        return;
    }
    struct tm now;
    env->clock->now_local(env->clock->ctx, &now);
    log_append_attempt(env->log, &now, req, resp, attempt, TS_MAX_ATTEMPTS);
    if (log_take_warning(env->log)) {
        env->ui->show_line(env->ui->ctx, TXT_LOG_UNAVAILABLE);
    }
}

static void run_attempt(const sync_env_t *env, const http_request_t *req, http_response_t *out, int attempt)
{
    http_response_init(out);
    env->net->perform(env->net->ctx, req, out);
    log_attempt(env, req, out, attempt);
}

static void announce_retry(const sync_env_t *env)
{
    char line[TS_LINE_SIZE];
    snprintf(line, sizeof line, TXT_RETRYING, TS_RETRY_DELAY_S);
    env->ui->show_line(env->ui->ctx, line);
    env->clock->sleep_seconds(env->clock->ctx, TS_RETRY_DELAY_S);
}

void retry_perform(const sync_env_t *env, const http_request_t *req, http_response_t *out)
{
    int attempt = 1;
    run_attempt(env, req, out, attempt);
    while (outcome_should_retry(out->curl_code, attempt, TS_MAX_ATTEMPTS)) {
        announce_retry(env);
        http_response_free(out);
        attempt++;
        run_attempt(env, req, out, attempt);
    }
}
