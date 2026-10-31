// Minimal WebDAV client over libcurl (PROPFIND, MKCOL, PUT, MOVE, GET).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "util.h"

typedef struct {
    long http_code;       // 0 if no response
    int curl_code;        // CURLcode, 0 = OK
    char curl_err[256];   // curl's error buffer
    double seconds;       // duration of the last attempt
    int attempts;
} DavResult;

// Options for PUT. Defaults (all zero) are the safe ones from the spec:
// Content-Length from the buffer, no "Expect: 100-continue", HTTP/1.1.
typedef struct {
    bool send_expect;   // keep curl's default "Expect: 100-continue" (experiment only)
    bool chunked;       // use chunked transfer encoding (experiment only)
    bool any_http;      // let curl pick HTTP version (experiment only)
    long upload_buffer; // CURLOPT_UPLOAD_BUFFERSIZE, 0 = curl default
} PutOptions;

typedef void (*DavProgressFn)(void *ctx, int64_t done, int64_t total);

bool dav_global_init(void);
void dav_global_cleanup(void);
void dav_set_progress(DavProgressFn fn, void *ctx);
// Number of extra attempts after a network error / 5xx (default 2).
void dav_set_retries(int retries, unsigned pause_ms);

// Builds the URL for "<basepath>/<name>" (name may be NULL for the folder itself).
bool dav_url(const Config *cfg, const char *name, char *out, size_t out_size);

DavResult dav_propfind(const Config *cfg, const char *name, int depth, Buffer *body);
DavResult dav_mkcol_path(const Config *cfg, const char *path_from_origin);
DavResult dav_put(const Config *cfg, const char *name, const void *data, size_t size, const PutOptions *opt);
DavResult dav_move(const Config *cfg, const char *from_name, const char *to_name);
DavResult dav_get(const Config *cfg, const char *name, Buffer *out);

// PROPFIND on the folder; on 404 creates every segment with MKCOL.
bool dav_ensure_folder(const Config *cfg, DavResult *last);

bool dav_ok(const DavResult *r);
// Human-readable explanation of a failed result.
const char *dav_explain(const DavResult *r);
