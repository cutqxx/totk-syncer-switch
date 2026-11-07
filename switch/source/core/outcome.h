#ifndef TS_OUTCOME_H
#define TS_OUTCOME_H

#include <stdbool.h>

/* Mirrors of CURLE_* values; http_curl.c asserts they match libcurl. */
enum {
    TS_CURLE_OK = 0,
    TS_CURLE_RESOLVE = 6,
    TS_CURLE_CONNECT = 7,
    TS_CURLE_TIMEOUT = 28,
    TS_CURLE_RECV = 56
};

typedef enum {
    TRANSPORT_OK,          /* an HTTP response arrived */
    TRANSPORT_UNREACHABLE, /* curl 6 / 7 / 28 */
    TRANSPORT_DROPPED,     /* curl 56 */
    TRANSPORT_OTHER
} transport_kind_t;

transport_kind_t outcome_classify(int curl_code);

/* Only curl 6/7/28/56 are transient; HTTP status codes never trigger a retry. */
bool outcome_should_retry(int curl_code, int attempt, int max_attempts);

#endif
