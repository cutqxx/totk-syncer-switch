#include "webdav.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

#define USER_AGENT "totk-sync/1.0"
#define CONNECT_TIMEOUT_S 15L
#define SHORT_TIMEOUT_S 60L      // PROPFIND / MKCOL / MOVE
#define TRANSFER_TIMEOUT_S 600L  // PUT / GET (stalls are caught by LOW_SPEED below)
#define LOW_SPEED_LIMIT_BPS 512L
#define LOW_SPEED_TIME_S 30L

static DavProgressFn g_progress;
static void *g_progress_ctx;
static int g_retries = 2;
static unsigned g_retry_pause_ms = 3000;

bool dav_global_init(void) { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; }
void dav_global_cleanup(void) { curl_global_cleanup(); }

void dav_set_progress(DavProgressFn fn, void *ctx) {
    g_progress = fn;
    g_progress_ctx = ctx;
}

void dav_set_retries(int retries, unsigned pause_ms) {
    g_retries = retries < 0 ? 0 : retries;
    g_retry_pause_ms = pause_ms;
}

static bool path_url(const Config *cfg, const char *path_from_origin, char *out, size_t out_size) {
    char enc[1536];
    if (!url_encode_path(enc, sizeof enc, path_from_origin)) return false;
    int n = snprintf(out, out_size, "%s/%s", cfg->origin, enc);
    return n > 0 && (size_t)n < out_size;
}

bool dav_url(const Config *cfg, const char *name, char *out, size_t out_size) {
    char path[1024];
    if (name && *name)
        snprintf(path, sizeof path, "%s/%s", cfg->basepath, name);
    else
        snprintf(path, sizeof path, "%s/", cfg->basepath);
    return path_url(cfg, path, out, out_size);
}

// ---------------------------------------------------------------------------

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} ReadState;

static size_t read_cb(char *buf, size_t size, size_t n, void *ud) {
    ReadState *rs = ud;
    size_t want = size * n;
    size_t left = rs->size - rs->pos;
    if (want > left) want = left;
    memcpy(buf, rs->data + rs->pos, want);
    rs->pos += want;
    return want;
}

static int seek_cb(void *ud, curl_off_t offset, int origin) {
    ReadState *rs = ud;
    if (origin != SEEK_SET || offset < 0 || (size_t)offset > rs->size) return CURL_SEEKFUNC_CANTSEEK;
    rs->pos = (size_t)offset;
    return CURL_SEEKFUNC_OK;
}

static size_t write_cb(char *ptr, size_t size, size_t n, void *ud) {
    Buffer *b = ud;
    size_t len = size * n;
    if (!b) return len;
    return buf_append(b, ptr, len) ? len : 0;
}

static int xfer_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ud;
    if (g_progress) {
        if (ultotal > 0)
            g_progress(g_progress_ctx, (int64_t)ulnow, (int64_t)ultotal);
        else if (dltotal > 0 || dlnow > 0)
            g_progress(g_progress_ctx, (int64_t)dlnow, (int64_t)dltotal);
    }
    return 0;
}

typedef enum { REQ_PROPFIND, REQ_MKCOL, REQ_PUT, REQ_MOVE, REQ_GET } ReqKind;

typedef struct {
    ReqKind kind;
    const char *url;
    int depth;                 // PROPFIND
    const char *destination;   // MOVE
    const uint8_t *data;       // PUT
    size_t size;               // PUT
    const PutOptions *put;     // PUT
    Buffer *body;              // response body sink (may be NULL)
} Request;

static const char *kind_name(ReqKind k) {
    switch (k) {
        case REQ_PROPFIND: return "PROPFIND";
        case REQ_MKCOL: return "MKCOL";
        case REQ_PUT: return "PUT";
        case REQ_MOVE: return "MOVE";
        case REQ_GET: return "GET";
    }
    return "?";
}

static DavResult perform_once(const Config *cfg, const Request *rq) {
    DavResult r;
    memset(&r, 0, sizeof r);
    CURL *c = curl_easy_init();
    if (!c) {
        r.curl_code = CURLE_FAILED_INIT;
        str_copy(r.curl_err, sizeof r.curl_err, "curl_easy_init failed");
        return r;
    }
    struct curl_slist *hdr = NULL;
    ReadState rs = {rq->data, rq->size, 0};
    static const char propfind_body[] =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<d:propfind xmlns:d=\"DAV:\"><d:prop>"
        "<d:resourcetype/><d:getlastmodified/><d:getcontentlength/>"
        "</d:prop></d:propfind>";

    curl_easy_setopt(c, CURLOPT_URL, rq->url);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, r.curl_err);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
    curl_easy_setopt(c, CURLOPT_USERNAME, cfg->username);
    curl_easy_setopt(c, CURLOPT_PASSWORD, cfg->password);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    if (cfg->cainfo[0]) curl_easy_setopt(c, CURLOPT_CAINFO, cfg->cainfo);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, LOW_SPEED_LIMIT_BPS);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, LOW_SPEED_TIME_S);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, rq->body);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer_cb);
    bool any_http = rq->kind == REQ_PUT && rq->put && rq->put->any_http;
    if (!any_http) curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);

    switch (rq->kind) {
        case REQ_PROPFIND: {
            char depth[32];
            snprintf(depth, sizeof depth, "Depth: %d", rq->depth);
            hdr = curl_slist_append(hdr, depth);
            hdr = curl_slist_append(hdr, "Content-Type: application/xml; charset=utf-8");
            curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PROPFIND");
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, propfind_body);
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)(sizeof propfind_body - 1));
            curl_easy_setopt(c, CURLOPT_TIMEOUT, SHORT_TIMEOUT_S);
            break;
        }
        case REQ_MKCOL:
            curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "MKCOL");
            curl_easy_setopt(c, CURLOPT_TIMEOUT, SHORT_TIMEOUT_S);
            break;
        case REQ_MOVE: {
            char dest[1700];
            snprintf(dest, sizeof dest, "Destination: %s", rq->destination);
            hdr = curl_slist_append(hdr, dest);
            hdr = curl_slist_append(hdr, "Overwrite: F");
            curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "MOVE");
            curl_easy_setopt(c, CURLOPT_TIMEOUT, SHORT_TIMEOUT_S);
            break;
        }
        case REQ_GET:
            curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(c, CURLOPT_TIMEOUT, TRANSFER_TIMEOUT_S);
            break;
        case REQ_PUT: {
            const PutOptions *o = rq->put;
            curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(c, CURLOPT_READFUNCTION, read_cb);
            curl_easy_setopt(c, CURLOPT_READDATA, &rs);
            curl_easy_setopt(c, CURLOPT_SEEKFUNCTION, seek_cb);
            curl_easy_setopt(c, CURLOPT_SEEKDATA, &rs);
            curl_easy_setopt(c, CURLOPT_TIMEOUT, TRANSFER_TIMEOUT_S);
            if (o && o->chunked)
                hdr = curl_slist_append(hdr, "Transfer-Encoding: chunked");
            else
                curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, (curl_off_t)rq->size);
            if (!(o && o->send_expect)) hdr = curl_slist_append(hdr, "Expect:");
            if (o && o->upload_buffer > 0) curl_easy_setopt(c, CURLOPT_UPLOAD_BUFFERSIZE, o->upload_buffer);
            hdr = curl_slist_append(hdr, "Content-Type: application/zip");
            break;
        }
    }
    if (hdr) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);

    double t0 = now_seconds();
    CURLcode cc = curl_easy_perform(c);
    r.seconds = now_seconds() - t0;
    r.curl_code = (int)cc;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.http_code);
    if (cc == CURLE_OK) r.curl_err[0] = 0;
    else if (!r.curl_err[0]) str_copy(r.curl_err, sizeof r.curl_err, curl_easy_strerror(cc));
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return r;
}

static bool retryable(const DavResult *r) {
    if (r->curl_code != 0) {
        // Configuration/TLS problems won't fix themselves.
        switch (r->curl_code) {
            case CURLE_UNSUPPORTED_PROTOCOL:
            case CURLE_URL_MALFORMAT:
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_CACERT_BADFILE:
            case CURLE_LOGIN_DENIED:
                return false;
            default:
                return true;
        }
    }
    return r->http_code >= 500 || r->http_code == 429;
}

static DavResult perform(const Config *cfg, const Request *rq) {
    DavResult r;
    for (int attempt = 0;; attempt++) {
        if (rq->body) rq->body->size = 0;
        r = perform_once(cfg, rq);
        r.attempts = attempt + 1;
        log_msg("%s %s -> http=%ld curl=%d%s%s (%.1fs, attempt %d)", kind_name(rq->kind), rq->url, r.http_code,
                r.curl_code, r.curl_err[0] ? " " : "", r.curl_err, r.seconds, r.attempts);
        if (dav_ok(&r) || !retryable(&r) || attempt >= g_retries) break;
        sleep_ms(g_retry_pause_ms);
    }
    return r;
}

// ---------------------------------------------------------------------------

DavResult dav_propfind(const Config *cfg, const char *name, int depth, Buffer *body) {
    char url[1600];
    DavResult r = {0};
    if (!dav_url(cfg, name, url, sizeof url)) {
        r.curl_code = CURLE_URL_MALFORMAT;
        return r;
    }
    Request rq = {.kind = REQ_PROPFIND, .url = url, .depth = depth, .body = body};
    return perform(cfg, &rq);
}

DavResult dav_mkcol_path(const Config *cfg, const char *path_from_origin) {
    char url[1600];
    DavResult r = {0};
    if (!path_url(cfg, path_from_origin, url, sizeof url)) {
        r.curl_code = CURLE_URL_MALFORMAT;
        return r;
    }
    Request rq = {.kind = REQ_MKCOL, .url = url};
    return perform(cfg, &rq);
}

DavResult dav_put(const Config *cfg, const char *name, const void *data, size_t size, const PutOptions *opt) {
    char url[1600];
    DavResult r = {0};
    if (!dav_url(cfg, name, url, sizeof url)) {
        r.curl_code = CURLE_URL_MALFORMAT;
        return r;
    }
    PutOptions def = {0};
    Request rq = {.kind = REQ_PUT, .url = url, .data = data, .size = size, .put = opt ? opt : &def};
    return perform(cfg, &rq);
}

DavResult dav_move(const Config *cfg, const char *from_name, const char *to_name) {
    char url[1600], dest[1600];
    DavResult r = {0};
    if (!dav_url(cfg, from_name, url, sizeof url) || !dav_url(cfg, to_name, dest, sizeof dest)) {
        r.curl_code = CURLE_URL_MALFORMAT;
        return r;
    }
    Request rq = {.kind = REQ_MOVE, .url = url, .destination = dest};
    return perform(cfg, &rq);
}

DavResult dav_get(const Config *cfg, const char *name, Buffer *out) {
    char url[1600];
    DavResult r = {0};
    if (!dav_url(cfg, name, url, sizeof url)) {
        r.curl_code = CURLE_URL_MALFORMAT;
        return r;
    }
    Request rq = {.kind = REQ_GET, .url = url, .body = out};
    return perform(cfg, &rq);
}

bool dav_ensure_folder(const Config *cfg, DavResult *last) {
    Buffer body = {0};
    DavResult r = dav_propfind(cfg, NULL, 0, &body);
    buf_free(&body);
    if (last) *last = r;
    if (dav_ok(&r)) return true;
    if (r.http_code != 404 && r.http_code != 409) return false;

    // Create each segment: "JKSV", "JKSV/The Legend ...".
    char path[600];
    if (!str_copy(path, sizeof path, cfg->basepath)) return false;
    size_t len = strlen(path);
    for (size_t i = 0; i <= len; i++) {
        if (path[i] != '/' && path[i] != 0) continue;
        char save = path[i];
        path[i] = 0;
        char seg[610];
        snprintf(seg, sizeof seg, "%s/", path);
        r = dav_mkcol_path(cfg, seg);
        path[i] = save;
        if (last) *last = r;
        // 405 = already exists.
        if (!dav_ok(&r) && r.http_code != 405) return false;
    }
    return true;
}

bool dav_ok(const DavResult *r) {
    return r->curl_code == 0 && r->http_code >= 200 && r->http_code < 300;
}

const char *dav_explain(const DavResult *r) {
    if (r->curl_code != 0) {
        switch (r->curl_code) {
            case CURLE_COULDNT_RESOLVE_HOST: return "Cannot resolve host: no internet or wrong origin.";
            case CURLE_COULDNT_CONNECT: return "Cannot connect to the server.";
            case CURLE_OPERATION_TIMEDOUT: return "Timeout: network too slow or server not answering.";
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_CACERT_BADFILE:
                return "TLS certificate check failed (see README: cainfo).";
            case CURLE_SSL_CONNECT_ERROR: return "TLS handshake failed.";
            case CURLE_SEND_ERROR: return "Connection broke while sending data.";
            case CURLE_RECV_ERROR: return "Connection broke while receiving data (curl 56).";
            case CURLE_URL_MALFORMAT: return "Bad URL: check origin/basepath in the config.";
            default: return "Network error.";
        }
    }
    switch (r->http_code) {
        case 401: return "Wrong login or app password (401).";
        case 403: return "Access denied (403).";
        case 404: return "Remote folder or file not found (404).";
        case 405: return "Method not allowed (405).";
        case 409: return "Parent folder missing (409).";
        case 412: return "Target already exists (412).";
        case 413: return "File too large for the server (413).";
        case 423: return "Resource is locked (423).";
        case 507: return "Not enough space on the Disk (507).";
        default: break;
    }
    if (r->http_code >= 500) return "Server error, try again later.";
    if (r->http_code >= 200 && r->http_code < 300) return "OK.";
    return "Unexpected server answer.";
}
