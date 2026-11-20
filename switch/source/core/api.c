#include "api.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "outcome.h"

#define APP_NAME "totk-sync"
#define API_VERSION 1
#define BEARER_PREFIX "Authorization: Bearer "
#define MODIFIED_PATTERN "dddd-dd-ddTdd:dd"

_Static_assert(TS_HEADER_LINE_SIZE > sizeof BEARER_PREFIX + TS_TOKEN_SIZE, "Authorization header must fit its line");

void api_add_authorization(http_request_t *req, const config_t *cfg)
{
    char authorization[TS_HEADER_LINE_SIZE];
    snprintf(authorization, sizeof authorization, BEARER_PREFIX "%s", cfg->token);
    http_request_add_header(req, authorization);
}

void api_format_url(const config_t *cfg, const char *path, char *out, size_t out_size)
{
    snprintf(out, out_size, "http://%s:%d%s", cfg->pc_host, cfg->pc_port, path);
}

void api_build_status_request(const config_t *cfg, http_request_t *out)
{
    http_request_init(out, HTTP_GET);
    api_format_url(cfg, "/status", out->url, sizeof out->url);
    api_add_authorization(out, cfg);
}

static bool matches_pattern_char(char pattern, char c)
{
    return pattern == 'd' ? (c >= '0' && c <= '9') : pattern == c;
}

bool api_format_modified(const char *iso8601, char out[API_MODIFIED_SIZE])
{
    size_t length = strlen(MODIFIED_PATTERN);
    if (strlen(iso8601) < length) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (!matches_pattern_char(MODIFIED_PATTERN[i], iso8601[i])) {
            return false;
        }
    }
    snprintf(out, API_MODIFIED_SIZE, "%.10s %.5s", iso8601, iso8601 + 11);
    return true;
}

static bool is_launcher_document(const cJSON *root)
{
    const cJSON *app = cJSON_GetObjectItemCaseSensitive(root, "app");
    const cJSON *api = cJSON_GetObjectItemCaseSensitive(root, "api");
    return cJSON_IsString(app) && strcmp(app->valuestring, APP_NAME) == 0
        && cJSON_IsNumber(api) && api->valuedouble == API_VERSION;
}

static bool read_save(const cJSON *save, api_status_t *out)
{
    const cJSON *present = cJSON_GetObjectItemCaseSensitive(save, "present");
    if (!cJSON_IsBool(present)) {
        return false;
    }
    out->save_present = cJSON_IsTrue(present);
    if (!out->save_present) {
        return true;
    }
    const cJSON *modified = cJSON_GetObjectItemCaseSensitive(save, "modified");
    return cJSON_IsString(modified) && api_format_modified(modified->valuestring, out->save_modified);
}

static bool read_status_fields(const cJSON *root, api_status_t *out)
{
    const cJSON *game_running = cJSON_GetObjectItemCaseSensitive(root, "game_running");
    const cJSON *save = cJSON_GetObjectItemCaseSensitive(root, "save");
    if (!cJSON_IsBool(game_running) || !cJSON_IsObject(save)) {
        return false;
    }
    out->game_running = cJSON_IsTrue(game_running);
    return read_save(save, out);
}

static api_status_kind_t interpret_status_body(const http_response_t *resp, api_status_t *out)
{
    if (resp->body.data == NULL) {
        return API_STATUS_NOT_LAUNCHER;
    }
    cJSON *root = cJSON_ParseWithLength((const char *)resp->body.data, (size_t)resp->body.size);
    if (root == NULL || !is_launcher_document(root)) {
        cJSON_Delete(root);
        return API_STATUS_NOT_LAUNCHER;
    }
    bool is_valid = read_status_fields(root, out);
    cJSON_Delete(root);
    return is_valid ? API_STATUS_OK : API_STATUS_BAD_RESPONSE;
}

static api_status_kind_t interpret_http(const http_response_t *resp, api_status_t *out)
{
    if (!http_response_has_header(resp, API_MARKER_HEADER, API_MARKER_VALUE)) {
        return API_STATUS_NOT_LAUNCHER;
    }
    switch (resp->http_code) {
    case 200: return interpret_status_body(resp, out);
    case 401: return API_STATUS_WRONG_TOKEN;
    case 403: return API_STATUS_FORBIDDEN;
    default: return API_STATUS_HTTP_ERROR;
    }
}

static api_status_kind_t interpret_transport_or_http(const http_response_t *resp, api_status_t *out)
{
    switch (outcome_classify(resp->curl_code)) {
    case TRANSPORT_OK: return interpret_http(resp, out);
    case TRANSPORT_UNREACHABLE: return API_STATUS_UNREACHABLE;
    default: return API_STATUS_NETWORK_ERROR;
    }
}

void api_interpret_status(const http_response_t *resp, api_status_t *out)
{
    memset(out, 0, sizeof *out);
    out->http_code = resp->http_code;
    out->curl_code = resp->curl_code;
    out->kind = interpret_transport_or_http(resp, out);
}
