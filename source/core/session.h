#ifndef TS_SESSION_H
#define TS_SESSION_H

#include <time.h>

#include "body.h"
#include "config.h"
#include "experiment.h"
#include "log.h"
#include "outcome.h"
#include "request.h"

typedef struct {
    void *ctx;
    void (*perform)(void *ctx, const upload_request_t *req, const body_t *body, upload_outcome_t *out);
    void (*sleep_seconds)(void *ctx, int seconds);
    void (*now_local)(void *ctx, struct tm *out);
    void (*show_line)(void *ctx, const char *line);
} upload_port_t;

typedef struct {
    const config_t *config;
    const char *sd_root;
    const experiment_t *experiment; /* NULL: no variant tag */
    log_sink_t *log;                /* NULL: no logging */
} session_inputs_t;

outcome_kind_t session_upload_test_file(const session_inputs_t *in, const upload_port_t *port);

#endif
