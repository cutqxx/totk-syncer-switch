#include "experiment.h"

#include <stdio.h>
#include <string.h>

#define ORIGIN_COM "https://webdav.yandex.com"
#define ORIGIN_RU "https://webdav.yandex.ru"
#define CONNECT_TIMEOUT_S 15L
#define TOTAL_TIMEOUT_S 120L
#define LONG_CONNECT_TIMEOUT_S 30L
#define LONG_TOTAL_TIMEOUT_S 600L

static bool is_origin(const char *origin, const char *expected)
{
    size_t length = strlen(expected);
    if (strncmp(origin, expected, length) != 0) {
        return false;
    }
    for (const char *rest = origin + length; *rest != '\0'; rest++) {
        if (*rest != '/') {
            return false;
        }
    }
    return true;
}

static exp_server_t server_of(const char *origin)
{
    if (is_origin(origin, ORIGIN_COM)) return EXP_SERVER_COM;
    if (is_origin(origin, ORIGIN_RU)) return EXP_SERVER_RU;
    return EXP_SERVER_OTHER;
}

static const char *server_tag(exp_server_t server)
{
    switch (server) {
    case EXP_SERVER_COM: return "com";
    case EXP_SERVER_RU: return "ru";
    default: return "other";
    }
}

experiment_t experiment_default(const char *config_origin)
{
    experiment_t e = { server_of(config_origin), false, false, false };
    return e;
}

static exp_server_t toggled_server(exp_server_t server)
{
    return server == EXP_SERVER_COM ? EXP_SERVER_RU : EXP_SERVER_COM;
}

void experiment_toggle(experiment_t *e, exp_param_t param)
{
    switch (param) {
    case EXP_PARAM_SERVER: e->server = toggled_server(e->server); break;
    case EXP_PARAM_EXPECT: e->is_expect_100 = !e->is_expect_100; break;
    case EXP_PARAM_BODY: e->is_chunked = !e->is_chunked; break;
    case EXP_PARAM_TIMEOUTS: e->is_long_timeouts = !e->is_long_timeouts; break;
    }
}

bool experiment_origin(const experiment_t *e, const char *config_origin, char *out, size_t out_size)
{
    const char *origin = config_origin;
    if (e->server == EXP_SERVER_COM) origin = ORIGIN_COM;
    if (e->server == EXP_SERVER_RU) origin = ORIGIN_RU;
    int length = snprintf(out, out_size, "%s", origin);
    return length >= 0 && (size_t)length < out_size;
}

long experiment_connect_timeout_s(const experiment_t *e)
{
    return e->is_long_timeouts ? LONG_CONNECT_TIMEOUT_S : CONNECT_TIMEOUT_S;
}

long experiment_total_timeout_s(const experiment_t *e)
{
    return e->is_long_timeouts ? LONG_TOTAL_TIMEOUT_S : TOTAL_TIMEOUT_S;
}

void experiment_tag(const experiment_t *e, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s|%s|%s|%s",
             server_tag(e->server),
             e->is_expect_100 ? "expect" : "noexpect",
             e->is_chunked ? "chunked" : "cl",
             e->is_long_timeouts ? "t600" : "t120");
}
