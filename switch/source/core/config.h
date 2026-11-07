#ifndef TS_CONFIG_H
#define TS_CONFIG_H

#include "ts_limits.h"

#define CONFIG_HOST_SIZE 16
#define CONFIG_PORT_MIN 1024
#define CONFIG_PORT_MAX 65535
#define CONFIG_DEFAULT_KEEP_BACKUPS 20

typedef struct {
    char pc_host[CONFIG_HOST_SIZE];
    int pc_port;
    char token[TS_TOKEN_SIZE];
    int keep_backups;
    char source_display_path[128];
} config_t;

typedef enum {
    CONFIG_OK,
    CONFIG_ERR_NOT_FOUND,
    CONFIG_ERR_PARSE,
    CONFIG_ERR_FIELD,
    CONFIG_ERR_FIELD_TOO_LONG,
    CONFIG_ERR_INVALID_VALUE,
    CONFIG_ERR_NOT_PRIVATE_IPV4,
    CONFIG_ERR_LEGACY_PHASE0,
    CONFIG_ERR_READ
} config_status_t;

typedef struct {
    config_status_t status;
    char message[512];
} config_error_t;

/* A leading UTF-8 BOM is skipped. Required: pc_host (private IPv4), pc_port (number 1024-65535),
   token (printable ASCII, no spaces, shorter than TS_TOKEN_SIZE). Optional: keep_backups (integer >= 1, default 20). */
config_status_t config_parse(const char *json, const char *display_path, config_t *out, config_error_t *err);
config_status_t config_load(const char *sd_root, config_t *out, config_error_t *err);

#endif
