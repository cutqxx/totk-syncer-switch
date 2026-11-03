#include "outcome.h"

#include <inttypes.h>
#include <stdio.h>

#include "text_ru.h"

static outcome_kind_t classify_curl_code(int curl_code)
{
    switch (curl_code) {
    case TS_CURLE_RESOLVE: return OUTCOME_NET_RESOLVE;
    case TS_CURLE_CONNECT: return OUTCOME_NET_CONNECT;
    case TS_CURLE_TIMEOUT: return OUTCOME_TIMEOUT;
    case TS_CURLE_RECV: return OUTCOME_NET_RECV;
    case TS_CURLE_SSL_CONNECT:
    case TS_CURLE_PEER_CERT: return OUTCOME_TLS;
    default: return OUTCOME_CURL_OTHER;
    }
}

static outcome_kind_t classify_http_code(long http_code)
{
    if (http_code >= 200 && http_code < 300) return OUTCOME_SUCCESS;
    if (http_code == 401) return OUTCOME_AUTH;
    if (http_code == 409) return OUTCOME_DIR_MISSING;
    if (http_code == 507) return OUTCOME_NO_SPACE;
    return OUTCOME_HTTP_OTHER;
}

outcome_kind_t outcome_classify(const upload_outcome_t *o)
{
    if (o->curl_code != TS_CURLE_OK) {
        return classify_curl_code(o->curl_code);
    }
    return classify_http_code(o->http_code);
}

bool outcome_should_retry(outcome_kind_t kind, int attempt, int max_attempts)
{
    bool is_transient = kind == OUTCOME_NET_RESOLVE || kind == OUTCOME_NET_CONNECT
                     || kind == OUTCOME_TIMEOUT || kind == OUTCOME_NET_RECV;
    return is_transient && attempt < max_attempts;
}

void outcome_message(const upload_outcome_t *o, char *out, size_t out_size)
{
    switch (outcome_classify(o)) {
    case OUTCOME_SUCCESS: snprintf(out, out_size, TXT_MSG_SUCCESS, o->http_code); break;
    case OUTCOME_AUTH: snprintf(out, out_size, "%s", TXT_MSG_AUTH); break;
    case OUTCOME_DIR_MISSING: snprintf(out, out_size, "%s", TXT_MSG_DIR_MISSING); break;
    case OUTCOME_NO_SPACE: snprintf(out, out_size, "%s", TXT_MSG_NO_SPACE); break;
    case OUTCOME_HTTP_OTHER: snprintf(out, out_size, TXT_MSG_HTTP_OTHER, o->http_code); break;
    case OUTCOME_NET_RESOLVE: snprintf(out, out_size, "%s", TXT_MSG_NET_RESOLVE); break;
    case OUTCOME_NET_CONNECT: snprintf(out, out_size, "%s", TXT_MSG_NET_CONNECT); break;
    case OUTCOME_TIMEOUT: snprintf(out, out_size, "%s", TXT_MSG_TIMEOUT); break;
    case OUTCOME_NET_RECV: snprintf(out, out_size, "%s", TXT_MSG_NET_RECV); break;
    case OUTCOME_TLS: snprintf(out, out_size, "%s", TXT_MSG_TLS); break;
    default: snprintf(out, out_size, TXT_MSG_CURL_OTHER, o->curl_code, o->curl_error); break;
    }
}

static const char *verdict_text(outcome_kind_t kind)
{
    return kind == OUTCOME_SUCCESS ? TXT_SUCCESS : TXT_FAILURE;
}

static void format_prefix(const struct tm *t, const char *variant_tag, char *out, size_t out_size)
{
    snprintf(out, out_size, "[%04d-%02d-%02d %02d:%02d:%02d]%s%s",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec,
             variant_tag != NULL ? " " : "", variant_tag != NULL ? variant_tag : "");
}

void outcome_format_line(const struct tm *t, const char *variant_tag, int attempt, int max_attempts, int64_t body_size, const upload_outcome_t *o, char *out, size_t out_size)
{
    char prefix[96];
    format_prefix(t, variant_tag, prefix, sizeof prefix);
    snprintf(out, out_size, "%s attempt %d/%d  HTTP %ld  curl %d (%s)  %.1f s  %" PRId64 "/%" PRId64 " B  %s",
             prefix, attempt, max_attempts, o->http_code, o->curl_code, o->curl_error,
             o->seconds, o->bytes_sent, body_size, verdict_text(outcome_classify(o)));
}
