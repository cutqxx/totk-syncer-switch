#ifndef TS_CONFIG_H
#define TS_CONFIG_H

typedef enum { CONFIG_SOURCE_TOTK_SYNC, CONFIG_SOURCE_JKSV } config_source_t;

typedef struct {
    char origin[256];
    char basepath[512];
    char username[128];
    char password[128];
    config_source_t source;
    char source_display_path[128];
} config_t;

typedef enum {
    CONFIG_OK,
    CONFIG_ERR_NOT_FOUND,
    CONFIG_ERR_PARSE,
    CONFIG_ERR_FIELD,
    CONFIG_ERR_FIELD_TOO_LONG,
    CONFIG_ERR_INSECURE_ORIGIN,
    CONFIG_ERR_READ
} config_status_t;

typedef struct {
    config_status_t status;
    char message[512];
} config_error_t;

config_status_t config_parse(const char *json, config_source_t source, const char *display_path, config_t *out, config_error_t *err);
config_status_t config_load(const char *sd_root, config_t *out, config_error_t *err);

#endif
