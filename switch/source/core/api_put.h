#ifndef TS_API_PUT_H
#define TS_API_PUT_H

#include <stdbool.h>
#include <stdint.h>

#include "api.h"
#include "body.h"
#include "config.h"
#include "http.h"

#define API_PUT_BACKUP_SIZE 128
#define API_PUT_FINGERPRINT_SIZE 72 /* "sha256:" + 64 hex + NUL */
#define API_PUT_REASON_SIZE 160

typedef enum {
    API_PUT_REPLACED,       /* 200, marker, and a body that matches contracts/api/put-replaced.json */
    API_PUT_UNREACHABLE,    /* curl 6 / 7 / 28 */
    API_PUT_NETWORK_ERROR,  /* any other curl failure with no usable HTTP answer */
    API_PUT_WRONG_TOKEN,    /* 401 from a launcher */
    API_PUT_FORBIDDEN,      /* 403 from a launcher */
    API_PUT_NOT_LAUNCHER,   /* no marker header */
    API_PUT_BAD_RESPONSE,   /* launcher answered 200 but the body is not a "replaced" result */
    API_PUT_INVALID_ZIP,    /* 400 invalid_zip; reason set */
    API_PUT_GAME_RUNNING,   /* 423 */
    API_PUT_BUSY,           /* 503 */
    API_PUT_NO_SAVE,        /* 404 no_save */
    API_PUT_WRITE_FAILED,   /* 500 write_failed; reason set (it names the PC backup) */
    API_PUT_HTTP_ERROR      /* any other code from a launcher */
} api_put_kind_t;

typedef struct {
    api_put_kind_t kind;
    long http_code;
    int curl_code;
    int64_t bytes_sent;
    char backup[API_PUT_BACKUP_SIZE];           /* API_PUT_REPLACED */
    char fingerprint[API_PUT_FINGERPRINT_SIZE]; /* API_PUT_REPLACED */
    char reason[API_PUT_REASON_SIZE];           /* printable ASCII; API_PUT_INVALID_ZIP and API_PUT_WRITE_FAILED */
} api_put_result_t;

/* Seconds allowed for the whole PUT: TS_UPLOAD_BASE_TIMEOUT_S plus the time at TS_UPLOAD_MIN_SPEED_BYTES_PER_S,
   at most TS_UPLOAD_MAX_TIMEOUT_S. */
long api_put_timeout_s(int64_t zip_bytes);

/* PUT http://<pc_host>:<pc_port>/save, Authorization, Content-Type: application/zip, Expect: 100-continue
   (so a refusal arrives before the body is sent). zip is not copied and must outlive the request. */
void api_build_put_request(const config_t *cfg, const body_t *zip, http_request_t *out);

/* Classifies a finished PUT /save. A refusal that arrived before the connection broke (curl 55/56 with an HTTP code of
   400 or more) counts as that refusal. Success needs 200, the marker header and a valid body. */
void api_interpret_put(const http_response_t *resp, api_put_result_t *out);

#endif
