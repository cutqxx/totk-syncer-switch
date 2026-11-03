#ifndef TS_LOG_H
#define TS_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "outcome.h"
#include "ts_limits.h"

typedef struct {
    char dir_path[TS_PATH_SIZE];
    char file_path[TS_PATH_SIZE];
    size_t sd_root_length;
    bool has_failed;
    bool is_warning_shown;
} log_sink_t;

void log_sink_init(log_sink_t *log, const char *sd_root);

/* Appends one redacted entry (never the body). Returns false and remembers the failure when it cannot be written. */
bool log_append_attempt(log_sink_t *log, const struct tm *t, const char *variant_tag, const char *url,
                        int64_t body_size, int attempt, int max_attempts, const upload_outcome_t *o,
                        const char *username, const char *password);

/* True exactly once after the first failure. */
bool log_take_warning(log_sink_t *log);

#endif
