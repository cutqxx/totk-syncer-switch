#ifndef TS_API_SAVE_H
#define TS_API_SAVE_H

#include <stdbool.h>

#include "api.h"
#include "api_put.h"
#include "body.h"
#include "config.h"
#include "http.h"

#define API_FINGERPRINT_HEADER "X-TotkSync-Fingerprint"
#define API_MODIFIED_HEADER "X-TotkSync-Modified"

typedef enum {
    API_GET_OK,            /* 200, marker, application/zip, a fingerprint and a modified header, a non-empty body */
    API_GET_UNREACHABLE,   /* curl 6 / 7 / 28 */
    API_GET_NETWORK_ERROR, /* any other curl failure */
    API_GET_WRONG_TOKEN,   /* 401 from a launcher */
    API_GET_FORBIDDEN,     /* 403 from a launcher */
    API_GET_NOT_LAUNCHER,  /* no marker header */
    API_GET_BAD_RESPONSE,  /* 200 from a launcher, but not a save ZIP with both headers */
    API_GET_NO_SAVE,       /* 404 no_save */
    API_GET_GAME_RUNNING,  /* 423 */
    API_GET_BUSY,          /* 503 */
    API_GET_READ_FAILED,   /* 500 read_failed; reason set */
    API_GET_HTTP_ERROR     /* any other code from a launcher */
} api_get_kind_t;

typedef struct {
    api_get_kind_t kind;
    long http_code;
    int curl_code;
    char fingerprint[API_PUT_FINGERPRINT_SIZE]; /* API_GET_OK */
    char modified[API_MODIFIED_SIZE];           /* API_GET_OK: "YYYY-MM-DD HH:MM" */
    char reason[API_PUT_REASON_SIZE];           /* API_GET_READ_FAILED, printable ASCII */
} api_get_result_t;

/* GET http://<pc_host>:<pc_port>/save with Authorization. The answer may be as large as a save ZIP (64 MiB), and the
   whole download gets TS_UPLOAD_MAX_TIMEOUT_S. */
void api_build_get_save_request(const config_t *cfg, http_request_t *out);

/* Classifies a finished GET /save; the ZIP stays in resp->body. */
void api_interpret_get_save(const http_response_t *resp, api_get_result_t *out);

/* POST /save/ack with {"fingerprint":"<fingerprint>"} written into *body (empty body expected, the caller frees it).
   False when fingerprint is not a valid fingerprint or memory runs out. */
bool api_build_ack_request(const config_t *cfg, const char *fingerprint, body_t *body, http_request_t *out);

/* True only for 200 with the marker and {"result":"recorded"} (contracts/api/ack-recorded.json). */
bool api_interpret_ack(const http_response_t *resp);

#endif
