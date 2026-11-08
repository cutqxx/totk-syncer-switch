#ifndef TS_RETRY_H
#define TS_RETRY_H

#include "ports.h"

/* Performs the request; after a transient network failure (curl 6/7/28/56) waits TS_RETRY_DELAY_S and tries once more.
   Every attempt is logged. *out holds the last attempt (initialised here; the caller frees it). */
void retry_perform(const sync_env_t *env, const http_request_t *req, http_response_t *out);

/* For requests that must not be repeated blindly (PUT /save): retries once, after TS_RETRY_DELAY_S, only when the
   connection could not be opened (curl 6/7), i.e. before any byte of the body was sent. A timeout or a dropped
   connection (28/55/56) is never retried: the PC may already have replaced its save. */
void retry_perform_unsent(const sync_env_t *env, const http_request_t *req, http_response_t *out);

#endif
