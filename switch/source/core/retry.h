#ifndef TS_RETRY_H
#define TS_RETRY_H

#include "ports.h"

/* Performs the request; after a transient network failure (curl 6/7/28/56) waits TS_RETRY_DELAY_S and tries once more.
   Every attempt is logged. *out holds the last attempt (initialised here; the caller frees it). */
void retry_perform(const sync_env_t *env, const http_request_t *req, http_response_t *out);

#endif
