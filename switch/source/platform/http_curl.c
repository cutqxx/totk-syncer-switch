#include "http_curl.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

#include "redact.h"
#include "ts_limits.h"

_Static_assert((int)CURLE_OK == TS_CURLE_OK, "curl code mirror");
_Static_assert((int)CURLE_COULDNT_RESOLVE_HOST == TS_CURLE_RESOLVE, "curl code mirror");
_Static_assert((int)CURLE_COULDNT_CONNECT == TS_CURLE_CONNECT, "curl code mirror");
_Static_assert((int)CURLE_OPERATION_TIMEDOUT == TS_CURLE_TIMEOUT, "curl code mirror");
_Static_assert((int)CURLE_SSL_CONNECT_ERROR == TS_CURLE_SSL_CONNECT, "curl code mirror");
_Static_assert((int)CURLE_RECV_ERROR == TS_CURLE_RECV, "curl code mirror");
_Static_assert((int)CURLE_PEER_FAILED_VERIFICATION == TS_CURLE_PEER_CERT, "curl code mirror");

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

static size_t discard_response(char *data, size_t item_size, size_t item_count, void *user)
{
    (void)data;
    (void)user;
    return item_size * item_count;
}

static int capture_headers(CURL *curl, curl_infotype type, char *data, size_t size, void *user)
{
    (void)curl;
    upload_outcome_t *out = user;
    if (type == CURLINFO_HEADER_OUT || type == CURLINFO_HEADER_IN) {
        redact_headers_append(out->headers, sizeof out->headers, data, size);
    }
    return 0;
}

static void set_error(upload_outcome_t *out, int code, const char *text)
{
    out->curl_code = code;
    snprintf(out->curl_error, sizeof out->curl_error, "%s", text);
}

static struct curl_slist *build_header_list(const upload_request_t *req)
{
    struct curl_slist *list = NULL;
    for (int i = 0; i < req->header_count; i++) {
        list = curl_slist_append(list, req->headers[i]);
    }
    return list;
}

static bool is_chunked(const upload_request_t *req)
{
    return req->content_length < 0;
}

static void configure_transfer(CURL *curl, const upload_request_t *req, read_source_t *source, struct curl_slist *headers)
{
    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    if (!is_chunked(req)) {
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)req->content_length);
    }
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_from_buffer);
    curl_easy_setopt(curl, CURLOPT_READDATA, source);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
}

static CURLcode configure_ca_file(CURL *curl, const upload_request_t *req)
{
    if (req->ca_file[0] == '\0') {
        return CURLE_OK;
    }
    return curl_easy_setopt(curl, CURLOPT_CAINFO, req->ca_file);
}

static CURLcode configure_auth_and_tls(CURL *curl, const upload_request_t *req)
{
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, req->username);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, req->password);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    CURLcode ca_result = configure_ca_file(curl, req);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, req->connect_timeout_s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, req->total_timeout_s);
    return ca_result;
}

static void reredact_headers(char *headers, size_t size)
{
    char redacted[TS_HEADERS_SIZE] = "";
    redact_headers_append(redacted, sizeof redacted, headers, strlen(headers));
    snprintf(headers, size, "%s", redacted);
}

static void collect_outcome(CURL *curl, CURLcode code, const char *error_buffer, upload_outcome_t *out)
{
    curl_off_t uploaded = 0;
    double seconds = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->http_code);
    curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD_T, &uploaded);
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &seconds);
    out->bytes_sent = uploaded;
    out->seconds = seconds;
    set_error(out, (int)code, error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code));
}

bool http_curl_global_init(void)
{
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void http_curl_global_cleanup(void)
{
    curl_global_cleanup();
}

void http_curl_perform(void *ctx, const upload_request_t *req, const body_t *body, upload_outcome_t *out)
{
    (void)ctx;
    memset(out, 0, sizeof *out);
    if (!req->verify_peer || !req->verify_host) {
        set_error(out, CURLE_BAD_FUNCTION_ARGUMENT, "TLS verification must stay enabled");
        return;
    }
    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        set_error(out, CURLE_FAILED_INIT, "curl_easy_init failed");
        return;
    }
    char error_buffer[CURL_ERROR_SIZE] = "";
    read_source_t source = { body->data, (size_t)body->size, 0 };
    struct curl_slist *headers = build_header_list(req);
    configure_transfer(curl, req, &source, headers);
    CURLcode tls_setup = configure_auth_and_tls(curl, req);
    if (tls_setup != CURLE_OK) {
        set_error(out, (int)tls_setup, "CURLOPT_CAINFO not supported");
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return;
    }
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, capture_headers);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, out);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    CURLcode code = curl_easy_perform(curl);
    collect_outcome(curl, code, error_buffer, out);
    reredact_headers(out->headers, sizeof out->headers);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
