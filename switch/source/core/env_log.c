#include "env_log.h"

#include "text.h"

void env_log_note(const sync_env_t *env, const char *text)
{
    if (env->log == NULL) {
        return;
    }
    struct tm now;
    env->clock->now_local(env->clock->ctx, &now);
    log_append_note(env->log, &now, text);
    if (log_take_warning(env->log)) {
        env->ui->show_line(env->ui->ctx, TXT_LOG_UNAVAILABLE);
    }
}
