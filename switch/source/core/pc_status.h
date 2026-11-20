#ifndef TS_PC_STATUS_H
#define TS_PC_STATUS_H

#include "api.h"
#include "config.h"
#include "ports.h"

typedef enum { PC_STATUS_ONLINE, PC_STATUS_NOT_REACHABLE, PC_STATUS_ERROR } pc_status_kind_t;

typedef struct {
    pc_status_kind_t kind;
    char status_line[96]; /* "PC: online, save from 2026-11-03 14:25", "PC: not reachable", "PC: error", ... */
    char detail[256];     /* error message for the user; empty when online */
} pc_status_t;

/* Pure: turns a classified /status answer into screen text. The config token is removed from the text. */
void pc_status_describe(const api_status_t *answer, const config_t *cfg, pc_status_t *out);

/* Startup / R scenario: GET /status with retry, then describe. */
void pc_status_refresh(const sync_env_t *env, const config_t *cfg, pc_status_t *out);

#endif
