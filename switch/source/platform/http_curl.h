#ifndef TS_HTTP_CURL_H
#define TS_HTTP_CURL_H

#include <stdbool.h>

#include "body.h"
#include "outcome.h"
#include "request.h"

bool http_curl_global_init(void);
void http_curl_global_cleanup(void);

/* Refuses to run (local error, no network) when TLS verification is not requested. */
void http_curl_perform(void *ctx, const upload_request_t *req, const body_t *body, upload_outcome_t *out);

#endif
