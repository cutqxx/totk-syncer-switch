#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "ipv4.h"
#include "paths.h"
#include "text.h"

#define UTF8_BOM "\xEF\xBB\xBF"
#define FIELD_PC_HOST "pc_host"
#define FIELD_PC_PORT "pc_port"
#define FIELD_TOKEN "token"
#define FIELD_KEEP_BACKUPS "keep_backups"

static config_status_t fail(config_error_t *err, config_status_t status, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

static config_status_t fail(config_error_t *err, config_status_t status, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    err->status = status;
    vsnprintf(err->message, sizeof err->message, format, args);
    va_end(args);
    return status;
}

static const cJSON *find_field(const cJSON *root, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(root, name);
}

static bool is_blank(const char *text)
{
    for (const char *p = text; *p != '\0'; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            return false;
        }
    }
    return true;
}

static bool is_whole_number_in_range(const cJSON *item, double min, double max)
{
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    double value = item->valuedouble;
    return value >= min && value <= max && value == (double)(long long)value;
}

static bool is_legacy_phase0(const cJSON *root)
{
    bool has_new_field = find_field(root, FIELD_PC_HOST) != NULL || find_field(root, FIELD_PC_PORT) != NULL
                      || find_field(root, FIELD_TOKEN) != NULL;
    bool has_old_field = find_field(root, "origin") != NULL || find_field(root, "username") != NULL
                      || find_field(root, "password") != NULL || find_field(root, "basepath") != NULL;
    return !has_new_field && has_old_field;
}

static config_status_t read_host(const cJSON *root, const char *display_path, config_t *out, config_error_t *err)
{
    const cJSON *item = find_field(root, FIELD_PC_HOST);
    if (!cJSON_IsString(item) || is_blank(item->valuestring)) {
        return fail(err, CONFIG_ERR_FIELD, TXT_CONFIG_FIELD_ERROR, FIELD_PC_HOST, display_path);
    }
    uint8_t octets[4];
    if (!ipv4_parse(item->valuestring, octets) || !ipv4_is_private(octets)) {
        return fail(err, CONFIG_ERR_NOT_PRIVATE_IPV4, TXT_CONFIG_NOT_PRIVATE_IPV4, display_path);
    }
    snprintf(out->pc_host, sizeof out->pc_host, "%s", item->valuestring);
    return CONFIG_OK;
}

static config_status_t read_port(const cJSON *root, const char *display_path, config_t *out, config_error_t *err)
{
    const cJSON *item = find_field(root, FIELD_PC_PORT);
    if (item == NULL) {
        return fail(err, CONFIG_ERR_FIELD, TXT_CONFIG_FIELD_ERROR, FIELD_PC_PORT, display_path);
    }
    if (!is_whole_number_in_range(item, CONFIG_PORT_MIN, CONFIG_PORT_MAX)) {
        return fail(err, CONFIG_ERR_INVALID_VALUE, TXT_CONFIG_PORT_INVALID, display_path);
    }
    out->pc_port = (int)item->valuedouble;
    return CONFIG_OK;
}

static bool is_printable_ascii_without_space(const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        if (*p <= ' ' || *p >= 0x7F) {
            return false;
        }
    }
    return true;
}

static config_status_t read_token(const cJSON *root, const char *display_path, config_t *out, config_error_t *err)
{
    const cJSON *item = find_field(root, FIELD_TOKEN);
    if (!cJSON_IsString(item) || is_blank(item->valuestring)) {
        return fail(err, CONFIG_ERR_FIELD, TXT_CONFIG_FIELD_ERROR, FIELD_TOKEN, display_path);
    }
    if (!is_printable_ascii_without_space(item->valuestring)) {
        return fail(err, CONFIG_ERR_INVALID_VALUE, TXT_CONFIG_TOKEN_INVALID, display_path);
    }
    if (strlen(item->valuestring) >= sizeof out->token) {
        return fail(err, CONFIG_ERR_FIELD_TOO_LONG, TXT_CONFIG_FIELD_TOO_LONG, FIELD_TOKEN, display_path, (int)sizeof out->token - 1);
    }
    strcpy(out->token, item->valuestring);
    return CONFIG_OK;
}

static config_status_t read_keep_backups(const cJSON *root, const char *display_path, config_t *out, config_error_t *err)
{
    const cJSON *item = find_field(root, FIELD_KEEP_BACKUPS);
    if (item == NULL) {
        out->keep_backups = CONFIG_DEFAULT_KEEP_BACKUPS;
        return CONFIG_OK;
    }
    if (!is_whole_number_in_range(item, 1, INT_MAX)) {
        return fail(err, CONFIG_ERR_INVALID_VALUE, TXT_CONFIG_KEEP_BACKUPS_INVALID, display_path);
    }
    out->keep_backups = (int)item->valuedouble;
    return CONFIG_OK;
}

static config_status_t read_fields(const cJSON *root, const char *display_path, config_t *out, config_error_t *err)
{
    if (is_legacy_phase0(root)) {
        return fail(err, CONFIG_ERR_LEGACY_PHASE0, "%s", TXT_CONFIG_LEGACY_PHASE0);
    }
    config_status_t status = read_host(root, display_path, out, err);
    if (status == CONFIG_OK) status = read_port(root, display_path, out, err);
    if (status == CONFIG_OK) status = read_token(root, display_path, out, err);
    if (status == CONFIG_OK) status = read_keep_backups(root, display_path, out, err);
    return status;
}

static void position_of(const char *json, const char *error_pointer, int *line, int *column)
{
    *line = 1;
    *column = 1;
    for (const char *cursor = json; cursor < error_pointer && *cursor != '\0'; cursor++) {
        if (*cursor == '\n') {
            (*line)++;
            *column = 1;
        } else {
            (*column)++;
        }
    }
}

static config_status_t fail_parse(const char *json, const char *parse_end, const char *display_path, config_error_t *err)
{
    const char *error_pointer = parse_end != NULL ? parse_end : cJSON_GetErrorPtr();
    if (error_pointer == NULL || error_pointer < json) {
        return fail(err, CONFIG_ERR_PARSE, TXT_CONFIG_PARSE_ERROR, display_path);
    }
    int line;
    int column;
    position_of(json, error_pointer, &line, &column);
    return fail(err, CONFIG_ERR_PARSE, TXT_CONFIG_PARSE_ERROR_AT, display_path, line, column);
}

config_status_t config_parse(const char *json, const char *display_path, config_t *out, config_error_t *err)
{
    if (strncmp(json, UTF8_BOM, 3) == 0) {
        json += 3;
    }
    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json, &parse_end, true);
    if (root == NULL) {
        return fail_parse(json, parse_end, display_path, err);
    }
    config_status_t status = read_fields(root, display_path, out, err);
    cJSON_Delete(root);
    if (status != CONFIG_OK) {
        return status;
    }
    snprintf(out->source_display_path, sizeof out->source_display_path, "%s", display_path);
    err->status = CONFIG_OK;
    err->message[0] = '\0';
    return CONFIG_OK;
}

typedef enum { READ_OK, READ_NOT_FOUND, READ_FAILED } read_status_t;

static long file_size_of(FILE *file)
{
    if (fseek(file, 0, SEEK_END) != 0) {
        return -1;
    }
    long size = ftell(file);
    return fseek(file, 0, SEEK_SET) == 0 ? size : -1;
}

static read_status_t read_open_file(FILE *file, char **text)
{
    long size = file_size_of(file);
    if (size < 0) {
        return READ_FAILED;
    }
    char *buffer = malloc((size_t)size + 1);
    if (buffer == NULL) {
        return READ_FAILED;
    }
    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        return READ_FAILED;
    }
    buffer[size] = '\0';
    *text = buffer;
    return READ_OK;
}

static read_status_t read_text_file(const char *path, char **text)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? READ_NOT_FOUND : READ_FAILED;
    }
    read_status_t status = read_open_file(file, text);
    fclose(file);
    return status;
}

config_status_t config_load(const char *sd_root, config_t *out, config_error_t *err)
{
    char fs_path[TS_PATH_SIZE];
    char display_path[128];
    paths_fs(sd_root, TS_REL_CONFIG, fs_path, sizeof fs_path);
    paths_display(TS_REL_CONFIG, display_path, sizeof display_path);
    char *json = NULL;
    read_status_t read_status = read_text_file(fs_path, &json);
    if (read_status == READ_NOT_FOUND) {
        return fail(err, CONFIG_ERR_NOT_FOUND, TXT_CONFIG_NOT_FOUND, display_path);
    }
    if (read_status == READ_FAILED) {
        return fail(err, CONFIG_ERR_READ, TXT_CONFIG_READ_ERROR, display_path);
    }
    config_status_t status = config_parse(json, display_path, out, err);
    free(json);
    return status;
}
