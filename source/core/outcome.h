#ifndef TS_OUTCOME_H
#define TS_OUTCOME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "ts_limits.h"

/* Mirrors of CURLE_* values; http_curl.c asserts they match libcurl. */
enum {
    TS_CURLE_OK = 0,
    TS_CURLE_RESOLVE = 6,
    TS_CURLE_CONNECT = 7,
    TS_CURLE_TIMEOUT = 28,
    TS_CURLE_SSL_CONNECT = 35,
    TS_CURLE_RECV = 56,
    TS_CURLE_PEER_CERT = 60
};

typedef struct {
    long http_code;
    int curl_code;
    char curl_error[256];
    double seconds;
    int64_t bytes_sent;
    char headers[TS_HEADERS_SIZE];
} upload_outcome_t;

typedef enum {
    OUTCOME_SUCCESS,
    OUTCOME_AUTH,
    OUTCOME_DIR_MISSING,
    OUTCOME_NO_SPACE,
    OUTCOME_HTTP_OTHER,
    OUTCOME_NET_RESOLVE,
    OUTCOME_NET_CONNECT,
    OUTCOME_TIMEOUT,
    OUTCOME_NET_RECV,
    OUTCOME_TLS,
    OUTCOME_CURL_OTHER,
    OUTCOME_PREFLIGHT_FAILED
} outcome_kind_t;

/* A curl error wins over the HTTP code; any 2xx is success. */
outcome_kind_t outcome_classify(const upload_outcome_t *o);

/* Retry only transient network failures, and only while attempts remain. */
bool outcome_should_retry(outcome_kind_t kind, int attempt, int max_attempts);

void outcome_message(const upload_outcome_t *o, char *out, size_t out_size);

/* variant_tag may be NULL (omitted from the line). */
void outcome_format_line(const struct tm *t, const char *variant_tag, int attempt, int max_attempts, int64_t body_size, const upload_outcome_t *o, char *out, size_t out_size);

#endif
