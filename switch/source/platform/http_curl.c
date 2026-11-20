#include "http_curl.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

#include "outcome.h"
#include "redact.h"

_Static_assert((int)CURLE_OK == TS_CURLE_OK, "curl code mirror");
_Static_assert((int)CURLE_COULDNT_RESOLVE_HOST == TS_CURLE_RESOLVE, "curl code mirror");
_Static_assert((int)CURLE_COULDNT_CONNECT == TS_CURLE_CONNECT, "curl code mirror");
_Static_assert((int)CURLE_OPERATION_TIMEDOUT == TS_CURLE_TIMEOUT, "curl code mirror");
_Static_assert((int)CURLE_SEND_ERROR == TS_CURLE_SEND, "curl code mirror");
_Static_assert((int)CURLE_RECV_ERROR == TS_CURLE_RECV, "curl code mirror");

typedef struct {
    http_response_t *response;
    int64_t max_response_bytes;
} transfer_t;

typedef struct {
    const unsigned char *data;
    size_t size;
    size_t offset;
} read_source_t;

static size_t read_from_buffer(char *dest, size_t item_size, size_t item_count, void *user)
{
    read_source_t *source = user;
    size_t wanted = item_size * item_count;
    size_t available = source->size - source->offset;
    size_t count = wanted < available ? wanted : available;
    memcpy(dest, source->data + source->offset, count);
    source->offset += count;
    return count;
}

static size_t store_response_body(char *data, size_t item_size, size_t item_count, void *user)
{
    transfer_t *transfer = user;
    size_t length = item_size * item_count;
    bool is_stored = body_append(&transfer->response->body, data, length, transfer->max_response_bytes);
    return is_stored ? length : 0;
}

static size_t store_response_header(char *data, size_t item_size, size_t item_count, void *user)
{
    transfer_t *transfer = user;
    size_t length = item_size * item_count;
    redact_headers_append(transfer->response->response_headers, sizeof transfer->response->response_headers, data, length);
    return length;
}

static int store_trace(CURL *curl, curl_infotype type, char *data, size_t size, void *user)
{
    (void)curl;
    transfer_t *transfer = user;
    if (type == CURLINFO_HEADER_OUT || type == CURLINFO_HEADER_IN) {
        redact_headers_append(transfer->response->trace, sizeof transfer->response->trace, data, size);
    }
    return 0;
}

static void set_error(http_response_t *out, int code, const char *text)
{
    out->curl_code = code;
    snprintf(out->curl_error, sizeof out->curl_error, "%s", text);
}

static struct curl_slist *build_header_list(const http_request_t *req)
{
    struct curl_slist *list = NULL;
    for (int i = 0; i < req->header_count; i++) {
        list = curl_slist_append(list, req->headers[i]);
    }
    return list;
}

static void configure_expect_100_timeout(CURL *curl, const http_request_t *req)
{
    if (req->expect_100_timeout_ms > 0) {
        curl_easy_setopt(curl, CURLOPT_EXPECT_100_TIMEOUT_MS, req->expect_100_timeout_ms);
    }
}

static void configure_request_body(CURL *curl, const http_request_t *req, read_source_t *source)
{
    int64_t size = req->body != NULL ? req->body->size : 0;
    if (req->method == HTTP_PUT) {
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)size);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_from_buffer);
        curl_easy_setopt(curl, CURLOPT_READDATA, source);
        configure_expect_100_timeout(curl, req);
    } else if (req->method == HTTP_POST) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, size > 0 ? (const char *)req->body->data : "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)size);
    }
}

static void configure_plain_http(CURL *curl, const http_request_t *req)
{
    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTP);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, req->connect_timeout_s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, req->total_timeout_s);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
}

static void configure_callbacks(CURL *curl, transfer_t *transfer, char *error_buffer)
{
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, store_response_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, transfer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, store_response_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, transfer);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, store_trace);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, transfer);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
}

static void reredact_trace(http_response_t *out)
{
    char redacted[TS_TRACE_SIZE] = "";
    redact_headers_append(redacted, sizeof redacted, out->trace, strlen(out->trace));
    snprintf(out->trace, sizeof out->trace, "%s", redacted);
}

static void collect_outcome(CURL *curl, CURLcode code, const char *error_buffer, http_response_t *out)
{
    curl_off_t uploaded = 0;
    double seconds = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->http_code);
    curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD_T, &uploaded);
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &seconds);
    out->bytes_sent = uploaded;
    out->seconds = seconds;
    set_error(out, (int)code, error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code));
    reredact_trace(out);
}

bool http_curl_global_init(void)
{
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void http_curl_global_cleanup(void)
{
    curl_global_cleanup();
}

void http_curl_perform(void *ctx, const http_request_t *req, http_response_t *out)
{
    (void)ctx;
    http_response_init(out);
    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        set_error(out, CURLE_FAILED_INIT, "curl_easy_init failed");
        return;
    }
    char error_buffer[CURL_ERROR_SIZE] = "";
    transfer_t transfer = { out, req->max_response_bytes };
    read_source_t source = { req->body != NULL ? req->body->data : NULL, req->body != NULL ? (size_t)req->body->size : 0, 0 };
    struct curl_slist *headers = build_header_list(req);
    configure_plain_http(curl, req);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    configure_request_body(curl, req, &source);
    configure_callbacks(curl, &transfer, error_buffer);
    CURLcode code = curl_easy_perform(curl);
    collect_outcome(curl, code, error_buffer, out);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
