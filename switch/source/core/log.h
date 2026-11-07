#ifndef TS_LOG_H
#define TS_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "http.h"
#include "ts_limits.h"

typedef struct {
    char dir_path[TS_PATH_SIZE];
    char file_path[TS_PATH_SIZE];
    char secret[TS_TOKEN_SIZE];
    size_t sd_root_length;
    bool has_failed;
    bool is_warning_shown;
} log_sink_t;

/* secret (the token) may be NULL; when set it is removed from every entry written. */
void log_sink_init(log_sink_t *log, const char *sd_root, const char *secret);

/* Appends one entry (never a request or response body): summary line, URL, redacted header trace.
   Authorization lines become "Authorization: ***" and the secret never appears.
   Returns false and remembers the failure when the entry cannot be written. */
bool log_append_attempt(log_sink_t *log, const struct tm *t, const http_request_t *req, const http_response_t *resp,
                        int attempt, int max_attempts);

/* True exactly once after the first failure. */
bool log_take_warning(log_sink_t *log);

#endif
