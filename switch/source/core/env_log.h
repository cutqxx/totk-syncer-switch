#ifndef TS_ENV_LOG_H
#define TS_ENV_LOG_H

#include "ports.h"

/* Writes one timestamped note to log.txt (a no-op when env->log is NULL).
   Shows "Log unavailable" once, the first time the file cannot be written. */
void env_log_note(const sync_env_t *env, const char *text);

#endif
