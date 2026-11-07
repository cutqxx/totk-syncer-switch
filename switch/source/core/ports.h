#ifndef TS_PORTS_H
#define TS_PORTS_H

#include <time.h>

#include "http.h"
#include "log.h"

/* perform fills *out completely (also on failure); the caller releases it with http_response_free. */
typedef struct {
    void *ctx;
    void (*perform)(void *ctx, const http_request_t *req, http_response_t *out);
} net_port_t;

typedef struct {
    void *ctx;
    void (*now_local)(void *ctx, struct tm *out);
    void (*sleep_seconds)(void *ctx, int seconds);
} clock_port_t;

typedef struct {
    void *ctx;
    void (*show_line)(void *ctx, const char *line);
} ui_port_t;

typedef struct {
    const net_port_t *net;
    const clock_port_t *clock;
    const ui_port_t *ui;
    log_sink_t *log; /* NULL: no logging */
} sync_env_t;

#endif
