#include "api_put.h"

#include <stdio.h>
#include <string.h>

#include "ascii_text.h"
#include "cJSON.h"
#include "outcome.h"

#define CONTENT_TYPE_HEADER "Content-Type: application/zip"
#define EXPECT_HEADER "Expect: 100-continue"
#define FINGERPRINT_PREFIX "sha256:"
#define FINGERPRINT_HEX_LENGTH 64
#define NO_DETAILS "no details"
#define MIN_ERROR_HTTP_CODE 400

long api_put_timeout_s(int64_t zip_bytes)
{
    int64_t transfer_s = (zip_bytes + TS_UPLOAD_MIN_SPEED_BYTES_PER_S - 1) / TS_UPLOAD_MIN_SPEED_BYTES_PER_S;
    int64_t total_s = TS_UPLOAD_BASE_TIMEOUT_S + transfer_s;
    return (long)(total_s > TS_UPLOAD_MAX_TIMEOUT_S ? TS_UPLOAD_MAX_TIMEOUT_S : total_s);
}

void api_build_put_request(const config_t *cfg, const body_t *zip, http_request_t *out)
{
    http_request_init(out, HTTP_PUT);
    api_format_url(cfg, "/save", out->url, sizeof out->url);
    api_add_authorization(out, cfg);
    http_request_add_header(out, CONTENT_TYPE_HEADER);
    http_request_add_header(out, EXPECT_HEADER);
    out->body = zip;
    out->total_timeout_s = api_put_timeout_s(zip->size);
    out->expect_100_timeout_ms = TS_EXPECT_100_TIMEOUT_MS;
}

static const char *string_field(const cJSON *root, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool is_lower_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static bool is_fingerprint(const char *text)
{
    size_t prefix_length = strlen(FINGERPRINT_PREFIX);
    if (strncmp(text, FINGERPRINT_PREFIX, prefix_length) != 0 || strlen(text) != prefix_length + FINGERPRINT_HEX_LENGTH) {
        return false;
    }
    for (const char *c = text + prefix_length; *c != '\0'; c++) {
        if (!is_lower_hex(*c)) {
            return false;
        }
    }
    return true;
}

static bool is_backup_name(const char *text)
{
    size_t length = strlen(text);
    if (length == 0 || length >= API_PUT_BACKUP_SIZE) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c <= 0x20 || c > 0x7E || c == '/' || c == '\\') {
            return false;
        }
    }
    return true;
}

static bool read_replaced_fields(const cJSON *root, api_put_result_t *out)
{
    const char *result = string_field(root, "result");
    const char *backup = string_field(root, "backup");
    const char *fingerprint = string_field(root, "fingerprint");
    if (result == NULL || strcmp(result, "replaced") != 0 || backup == NULL || !is_backup_name(backup)
        || fingerprint == NULL || !is_fingerprint(fingerprint)) {
        return false;
    }
    snprintf(out->backup, sizeof out->backup, "%s", backup);
    snprintf(out->fingerprint, sizeof out->fingerprint, "%s", fingerprint);
    return true;
}

static cJSON *parse_body(const http_response_t *resp)
{
    if (resp->body.data == NULL) {
        return NULL;
    }
    return cJSON_ParseWithLength((const char *)resp->body.data, (size_t)resp->body.size);
}

static api_put_kind_t interpret_replaced(const http_response_t *resp, api_put_result_t *out)
{
    cJSON *root = parse_body(resp);
    bool is_valid = root != NULL && read_replaced_fields(root, out);
    cJSON_Delete(root);
    return is_valid ? API_PUT_REPLACED : API_PUT_BAD_RESPONSE;
}

static bool is_error_document(const cJSON *root, const char *error_name)
{
    const char *error = root != NULL ? string_field(root, "error") : NULL;
    return error != NULL && strcmp(error, error_name) == 0;
}

static void read_reason(const cJSON *root, api_put_result_t *out)
{
    const char *reason = string_field(root, "reason");
    ascii_copy(reason != NULL ? reason : NO_DETAILS, out->reason, sizeof out->reason);
}

static bool carries_reason(api_put_kind_t kind)
{
    return kind == API_PUT_INVALID_ZIP || kind == API_PUT_WRITE_FAILED;
}

static api_put_kind_t interpret_error_document(const http_response_t *resp, const char *error_name, api_put_kind_t kind,
                                               api_put_result_t *out)
{
    cJSON *root = parse_body(resp);
    bool is_expected = is_error_document(root, error_name);
    if (is_expected && carries_reason(kind)) {
        read_reason(root, out);
    }
    cJSON_Delete(root);
    return is_expected ? kind : API_PUT_HTTP_ERROR;
}

static api_put_kind_t interpret_launcher_answer(const http_response_t *resp, api_put_result_t *out)
{
    switch (resp->http_code) {
    case 200: return interpret_replaced(resp, out);
    case 400: return interpret_error_document(resp, "invalid_zip", API_PUT_INVALID_ZIP, out);
    case 401: return API_PUT_WRONG_TOKEN;
    case 403: return API_PUT_FORBIDDEN;
    case 404: return interpret_error_document(resp, "no_save", API_PUT_NO_SAVE, out);
    case 423: return API_PUT_GAME_RUNNING;
    case 500: return interpret_error_document(resp, "write_failed", API_PUT_WRITE_FAILED, out);
    case 503: return API_PUT_BUSY;
    default: return API_PUT_HTTP_ERROR;
    }
}

static bool has_http_answer(const http_response_t *resp)
{
    if (outcome_classify(resp->curl_code) == TRANSPORT_OK) {
        return true;
    }
    bool is_connection_broken = resp->curl_code == TS_CURLE_SEND || resp->curl_code == TS_CURLE_RECV;
    return is_connection_broken && resp->http_code >= MIN_ERROR_HTTP_CODE;
}

static api_put_kind_t interpret_transport_or_http(const http_response_t *resp, api_put_result_t *out)
{
    if (!has_http_answer(resp)) {
        return outcome_classify(resp->curl_code) == TRANSPORT_UNREACHABLE ? API_PUT_UNREACHABLE : API_PUT_NETWORK_ERROR;
    }
    if (!http_response_has_header(resp, API_MARKER_HEADER, API_MARKER_VALUE)) {
        return API_PUT_NOT_LAUNCHER;
    }
    return interpret_launcher_answer(resp, out);
}

void api_interpret_put(const http_response_t *resp, api_put_result_t *out)
{
    memset(out, 0, sizeof *out);
    out->http_code = resp->http_code;
    out->curl_code = resp->curl_code;
    out->bytes_sent = resp->bytes_sent;
    out->kind = interpret_transport_or_http(resp, out);
}
