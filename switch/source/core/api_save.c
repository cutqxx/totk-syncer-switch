#include "api_save.h"

#include <stdio.h>
#include <string.h>

#include "ascii_text.h"
#include "cJSON.h"
#include "outcome.h"

#define ZIP_CONTENT_TYPE "application/zip"
#define JSON_CONTENT_TYPE_HEADER "Content-Type: application/json"
#define NO_DETAILS "no details"
#define HEADER_VALUE_SIZE 96

void api_build_get_save_request(const config_t *cfg, http_request_t *out)
{
    http_request_init(out, HTTP_GET);
    api_format_url(cfg, "/save", out->url, sizeof out->url);
    api_add_authorization(out, cfg);
    out->max_response_bytes = TS_MAX_SAVE_BYTES;
    out->total_timeout_s = TS_UPLOAD_MAX_TIMEOUT_S;
}

static cJSON *parse_body(const http_response_t *resp)
{
    if (resp->body.data == NULL) {
        return NULL;
    }
    return cJSON_ParseWithLength((const char *)resp->body.data, (size_t)resp->body.size);
}

static const char *string_field(const cJSON *root, const char *name)
{
    const cJSON *item = root != NULL ? cJSON_GetObjectItemCaseSensitive(root, name) : NULL;
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool is_error_document(const http_response_t *resp, const char *error_name, char *reason, size_t reason_size)
{
    cJSON *root = parse_body(resp);
    const char *error = string_field(root, "error");
    bool is_expected = error != NULL && strcmp(error, error_name) == 0;
    if (is_expected && reason != NULL) {
        const char *text = string_field(root, "reason");
        ascii_copy(text != NULL ? text : NO_DETAILS, reason, reason_size);
    }
    cJSON_Delete(root);
    return is_expected;
}

static api_get_kind_t interpret_zip(const http_response_t *resp, api_get_result_t *out)
{
    char content_type[HEADER_VALUE_SIZE];
    char fingerprint[HEADER_VALUE_SIZE];
    char modified[HEADER_VALUE_SIZE];
    bool is_zip = http_response_header_value(resp, "Content-Type", content_type, sizeof content_type)
                  && strcmp(content_type, ZIP_CONTENT_TYPE) == 0;
    bool has_fingerprint = http_response_header_value(resp, API_FINGERPRINT_HEADER, fingerprint, sizeof fingerprint)
                           && api_is_fingerprint(fingerprint);
    bool has_modified = http_response_header_value(resp, API_MODIFIED_HEADER, modified, sizeof modified)
                        && api_format_modified(modified, out->modified);
    if (!is_zip || !has_fingerprint || !has_modified || resp->body.size <= 0) {
        out->modified[0] = '\0';
        return API_GET_BAD_RESPONSE;
    }
    memcpy(out->fingerprint, fingerprint, strlen(fingerprint) + 1); /* api_is_fingerprint: exactly 71 characters */
    return API_GET_OK;
}

static api_get_kind_t interpret_launcher_answer(const http_response_t *resp, api_get_result_t *out)
{
    switch (resp->http_code) {
    case 200: return interpret_zip(resp, out);
    case 401: return API_GET_WRONG_TOKEN;
    case 403: return API_GET_FORBIDDEN;
    case 404: return is_error_document(resp, "no_save", NULL, 0) ? API_GET_NO_SAVE : API_GET_HTTP_ERROR;
    case 423: return API_GET_GAME_RUNNING;
    case 500:
        return is_error_document(resp, "read_failed", out->reason, sizeof out->reason) ? API_GET_READ_FAILED
                                                                                       : API_GET_HTTP_ERROR;
    case 503: return API_GET_BUSY;
    default: return API_GET_HTTP_ERROR;
    }
}

void api_interpret_get_save(const http_response_t *resp, api_get_result_t *out)
{
    memset(out, 0, sizeof *out);
    out->http_code = resp->http_code;
    out->curl_code = resp->curl_code;
    transport_kind_t transport = outcome_classify(resp->curl_code);
    if (transport != TRANSPORT_OK) {
        out->kind = transport == TRANSPORT_UNREACHABLE ? API_GET_UNREACHABLE : API_GET_NETWORK_ERROR;
    } else if (!http_response_has_header(resp, API_MARKER_HEADER, API_MARKER_VALUE)) {
        out->kind = API_GET_NOT_LAUNCHER;
    } else {
        out->kind = interpret_launcher_answer(resp, out);
    }
}

bool api_build_ack_request(const config_t *cfg, const char *fingerprint, body_t *body, http_request_t *out)
{
    char json[API_PUT_FINGERPRINT_SIZE + 32];
    if (!api_is_fingerprint(fingerprint)) {
        return false;
    }
    int length = snprintf(json, sizeof json, "{\"fingerprint\":\"%s\"}", fingerprint);
    if (!body_append(body, json, (size_t)length, TS_MAX_JSON_BYTES)) {
        return false;
    }
    http_request_init(out, HTTP_POST);
    api_format_url(cfg, "/save/ack", out->url, sizeof out->url);
    api_add_authorization(out, cfg);
    http_request_add_header(out, JSON_CONTENT_TYPE_HEADER);
    out->body = body;
    return true;
}

bool api_interpret_ack(const http_response_t *resp)
{
    if (outcome_classify(resp->curl_code) != TRANSPORT_OK || resp->http_code != 200
        || !http_response_has_header(resp, API_MARKER_HEADER, API_MARKER_VALUE)) {
        return false;
    }
    cJSON *root = parse_body(resp);
    const char *result = string_field(root, "result");
    bool is_recorded = result != NULL && strcmp(result, "recorded") == 0;
    cJSON_Delete(root);
    return is_recorded;
}
