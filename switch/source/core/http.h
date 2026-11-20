#ifndef TS_HTTP_H
#define TS_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "body.h"
#include "ts_limits.h"

typedef enum { HTTP_GET, HTTP_PUT, HTTP_POST } http_method_t;

typedef struct {
    http_method_t method;
    char url[TS_URL_SIZE];
    char headers[TS_MAX_HEADERS][TS_HEADER_LINE_SIZE];
    int header_count;
    const body_t *body; /* NULL: no request body */
    int64_t max_response_bytes;
    long connect_timeout_s;
    long total_timeout_s;
    long expect_100_timeout_ms; /* 0: curl default; otherwise how long to wait for "100 Continue" or an early final answer */
} http_request_t;

typedef struct {
    long http_code;
    int curl_code;
    char curl_error[256];
    double seconds;
    int64_t bytes_sent;
    char response_headers[TS_RESPONSE_HEADERS_SIZE]; /* raw header lines of every response block */
    char trace[TS_TRACE_SIZE];                        /* request and response headers, Authorization already redacted */
    body_t body;
} http_response_t;

/* Fills the 5 s / 60 s timeouts, the JSON response limit, and no headers or body. */
void http_request_init(http_request_t *req, http_method_t method);

/* Adds a header line ("Name: value"); silently ignored when the list is full, truncated at TS_HEADER_LINE_SIZE - 1. */
void http_request_add_header(http_request_t *req, const char *line);

const char *http_method_name(http_method_t method);

void http_response_init(http_response_t *resp);
void http_response_free(http_response_t *resp);

/* True when a header line with this name (case-insensitive) and exactly this value (surrounding blanks ignored) exists. */
bool http_response_has_header(const http_response_t *resp, const char *name, const char *value);

/* The value of the last header line with this name (case-insensitive), surrounding blanks removed; false when absent. */
bool http_response_header_value(const http_response_t *resp, const char *name, char *out, size_t out_size);

#endif
