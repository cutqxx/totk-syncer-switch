#include "outcome.h"

transport_kind_t outcome_classify(int curl_code)
{
    switch (curl_code) {
    case TS_CURLE_OK: return TRANSPORT_OK;
    case TS_CURLE_RESOLVE:
    case TS_CURLE_CONNECT:
    case TS_CURLE_TIMEOUT: return TRANSPORT_UNREACHABLE;
    case TS_CURLE_RECV: return TRANSPORT_DROPPED;
    default: return TRANSPORT_OTHER;
    }
}

bool outcome_should_retry(int curl_code, int attempt, int max_attempts)
{
    transport_kind_t kind = outcome_classify(curl_code);
    bool is_transient = kind == TRANSPORT_UNREACHABLE || kind == TRANSPORT_DROPPED;
    return is_transient && attempt < max_attempts;
}
