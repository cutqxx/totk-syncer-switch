#include "config.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "paths.h"
#include "text.h"

#define UTF8_BOM "\xEF\xBB\xBF"

typedef enum { FIELD_OK, FIELD_MISSING, FIELD_TOO_LONG } field_result_t;

typedef struct {
    const char *name;
    char *dest;
    size_t dest_size;
} field_t;

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

static field_result_t copy_string_field(const cJSON *root, const field_t *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, field->name);
    if (!cJSON_IsString(item) || item->valuestring[0] == '\0') {
        return FIELD_MISSING;
    }
    if (strlen(item->valuestring) >= field->dest_size) {
        return FIELD_TOO_LONG;
    }
    strcpy(field->dest, item->valuestring);
    return FIELD_OK;
}

static config_status_t read_fields(const cJSON *root, config_source_t source, const char *display_path, config_t *out, config_error_t *err)
{
    const field_t fields[] = {
        { "origin", out->origin, sizeof out->origin },
        { "basepath", out->basepath, sizeof out->basepath },
        { "username", out->username, sizeof out->username },
        { "password", out->password, sizeof out->password },
    };
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        field_result_t result = copy_string_field(root, &fields[i]);
        bool is_optional_basepath = source == CONFIG_SOURCE_JKSV && i == 1;
        if (result == FIELD_MISSING && is_optional_basepath) {
            out->basepath[0] = '\0';
        } else if (result == FIELD_MISSING) {
            return fail(err, CONFIG_ERR_FIELD, TXT_CONFIG_FIELD_ERROR, fields[i].name, display_path);
        } else if (result == FIELD_TOO_LONG) {
            return fail(err, CONFIG_ERR_FIELD_TOO_LONG, TXT_CONFIG_FIELD_TOO_LONG, fields[i].name, display_path, (int)fields[i].dest_size - 1);
        }
    }
    return CONFIG_OK;
}

static bool append_game_folder(char *basepath, size_t size)
{
    size_t length = strlen(basepath);
    while (length > 0 && basepath[length - 1] == '/') {
        length--;
    }
    basepath[length] = '\0';
    int written = snprintf(basepath + length, size - length, "%s%s", length > 0 ? "/" : "", TS_GAME_FOLDER);
    return written >= 0 && (size_t)written < size - length;
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

static config_status_t validate_fields(const cJSON *root, config_source_t source, const char *display_path, config_t *out, config_error_t *err)
{
    config_status_t status = read_fields(root, source, display_path, out, err);
    if (status != CONFIG_OK) {
        return status;
    }
    if (strncmp(out->origin, "https://", 8) != 0) {
        return fail(err, CONFIG_ERR_INSECURE_ORIGIN, TXT_CONFIG_INSECURE_ORIGIN, display_path);
    }
    if (source == CONFIG_SOURCE_JKSV && !append_game_folder(out->basepath, sizeof out->basepath)) {
        return fail(err, CONFIG_ERR_FIELD_TOO_LONG, TXT_CONFIG_BASEPATH_TOO_LONG, display_path);
    }
    return CONFIG_OK;
}

config_status_t config_parse(const char *json, config_source_t source, const char *display_path, config_t *out, config_error_t *err)
{
    if (strncmp(json, UTF8_BOM, 3) == 0) {
        json += 3;
    }
    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json, &parse_end, false);
    if (root == NULL) {
        return fail_parse(json, parse_end, display_path, err);
    }
    config_status_t status = validate_fields(root, source, display_path, out, err);
    cJSON_Delete(root);
    if (status != CONFIG_OK) {
        return status;
    }
    out->source = source;
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

static config_status_t load_candidate(const char *sd_root, const char *rel, config_source_t source, config_t *out, config_error_t *err, bool *was_found)
{
    char fs_path[256];
    char display_path[128];
    paths_fs(sd_root, rel, fs_path, sizeof fs_path);
    paths_display(rel, display_path, sizeof display_path);
    char *json = NULL;
    read_status_t read_status = read_text_file(fs_path, &json);
    *was_found = read_status != READ_NOT_FOUND;
    if (read_status == READ_NOT_FOUND) {
        return CONFIG_ERR_NOT_FOUND;
    }
    if (read_status == READ_FAILED) {
        return fail(err, CONFIG_ERR_READ, TXT_CONFIG_READ_ERROR, display_path);
    }
    config_status_t status = config_parse(json, source, display_path, out, err);
    free(json);
    return status;
}

static config_status_t fail_not_found(config_error_t *err)
{
    char totk_sync_display[128];
    char jksv_display[128];
    paths_display(TS_REL_CONFIG, totk_sync_display, sizeof totk_sync_display);
    paths_display(TS_REL_JKSV_CONFIG, jksv_display, sizeof jksv_display);
    return fail(err, CONFIG_ERR_NOT_FOUND, TXT_CONFIG_NOT_FOUND, totk_sync_display, jksv_display);
}

config_status_t config_load(const char *sd_root, config_t *out, config_error_t *err)
{
    bool was_found;
    config_status_t status = load_candidate(sd_root, TS_REL_CONFIG, CONFIG_SOURCE_TOTK_SYNC, out, err, &was_found);
    if (was_found) {
        return status;
    }
    status = load_candidate(sd_root, TS_REL_JKSV_CONFIG, CONFIG_SOURCE_JKSV, out, err, &was_found);
    return was_found ? status : fail_not_found(err);
}
