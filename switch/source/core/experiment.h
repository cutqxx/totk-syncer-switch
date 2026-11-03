#ifndef TS_EXPERIMENT_H
#define TS_EXPERIMENT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum { EXP_SERVER_COM, EXP_SERVER_RU, EXP_SERVER_OTHER } exp_server_t;

typedef struct {
    exp_server_t server;
    bool is_expect_100;
    bool is_chunked;
    bool is_long_timeouts;
} experiment_t;

typedef enum { EXP_PARAM_SERVER, EXP_PARAM_EXPECT, EXP_PARAM_BODY, EXP_PARAM_TIMEOUTS } exp_param_t;

/* Spec 5.4 defaults; the server variant is taken from the config origin. */
experiment_t experiment_default(const char *config_origin);

/* Flips one parameter. Server cycles com <-> ru; from "other" (custom config origin) it goes to com. */
void experiment_toggle(experiment_t *e, exp_param_t param);

/* Origin to upload to: .com / .ru for those variants, otherwise config_origin unchanged. */
bool experiment_origin(const experiment_t *e, const char *config_origin, char *out, size_t out_size);

long experiment_connect_timeout_s(const experiment_t *e);
long experiment_total_timeout_s(const experiment_t *e);

/* e.g. "com|noexpect|cl|t120" */
void experiment_tag(const experiment_t *e, char *out, size_t out_size);

#endif
