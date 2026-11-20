#ifndef TS_HTTP_CURL_H
#define TS_HTTP_CURL_H

#include <stdbool.h>

#include "http.h"

bool http_curl_global_init(void);
void http_curl_global_cleanup(void);

/* Plain http only (CURLOPT_PROTOCOLS = http, no redirects). Fills *out completely; the caller frees out->body. */
void http_curl_perform(void *ctx, const http_request_t *req, http_response_t *out);

#endif
