#ifndef TS_API_H
#define TS_API_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "http.h"

#define API_MARKER_HEADER "X-TotkSync-Api"
#define API_MARKER_VALUE "1"
#define API_MODIFIED_SIZE 17 /* "YYYY-MM-DD HH:MM" + NUL */

typedef enum {
    API_STATUS_OK,
    API_STATUS_UNREACHABLE,   /* curl 6 / 7 / 28 */
    API_STATUS_NETWORK_ERROR, /* any other curl failure */
    API_STATUS_WRONG_TOKEN,   /* 401 from a launcher */
    API_STATUS_FORBIDDEN,     /* 403 from a launcher */
    API_STATUS_NOT_LAUNCHER,  /* no marker header, or 200 whose JSON is not a TotK Sync v1 status */
    API_STATUS_BAD_RESPONSE,  /* launcher answered 200 but the status fields are malformed */
    API_STATUS_HTTP_ERROR     /* any other HTTP code from a launcher */
} api_status_kind_t;

typedef struct {
    api_status_kind_t kind;
    long http_code;
    int curl_code;
    bool game_running;
    bool save_present;
    char save_modified[API_MODIFIED_SIZE]; /* valid when save_present */
} api_status_t;

/* Adds "Authorization: Bearer <token>" (redacted to *** everywhere it is logged). */
void api_add_authorization(http_request_t *req, const config_t *cfg);

/* "http://<pc_host>:<pc_port><path>", path starting with '/'. */
void api_format_url(const config_t *cfg, const char *path, char *out, size_t out_size);

/* GET http://<pc_host>:<pc_port>/status with "Authorization: Bearer <token>". */
void api_build_status_request(const config_t *cfg, http_request_t *out);

/* Classifies a finished GET /status. The marker header is required on every launcher response, 401 included. */
void api_interpret_status(const http_response_t *resp, api_status_t *out);

/* "2026-11-03T14:25:01+03:00" -> "2026-11-03 14:25" (taken from the text, no clock math). False when the text is malformed. */
bool api_format_modified(const char *iso8601, char out[API_MODIFIED_SIZE]);

#endif
