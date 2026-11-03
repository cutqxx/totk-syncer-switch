#include "request.h"

#include <string.h>

#include "ts_limits.h"
#include "url.h"

static bool copy_ca_file(const char *ca_file, upload_request_t *out)
{
    if (ca_file == NULL) {
        out->ca_file[0] = '\0';
        return true;
    }
    if (strlen(ca_file) >= sizeof out->ca_file) {
        return false;
    }
    strcpy(out->ca_file, ca_file);
    return true;
}

static void set_body_and_headers(const experiment_t *experiment, int64_t body_size, upload_request_t *out)
{
    out->content_length = experiment->is_chunked ? -1 : body_size;
    if (!experiment->is_expect_100) {
        out->headers[out->header_count++] = "Expect:";
    }
    if (experiment->is_chunked) {
        out->headers[out->header_count++] = "Transfer-Encoding: chunked";
    }
}

bool request_build_put(const config_t *cfg, const char *remote_name, int64_t body_size, const char *ca_file,
                       const experiment_t *experiment, upload_request_t *out)
{
    experiment_t defaults = experiment_default(cfg->origin);
    const experiment_t *active = experiment != NULL ? experiment : &defaults;
    char origin[TS_URL_SIZE];

    memset(out, 0, sizeof *out);
    if (!experiment_origin(active, cfg->origin, origin, sizeof origin)) {
        return false;
    }
    if (!url_build_destination(origin, cfg->basepath, remote_name, out->url, sizeof out->url)) {
        return false;
    }
    if (!copy_ca_file(ca_file, out)) {
        return false;
    }
    out->username = cfg->username;
    out->password = cfg->password;
    set_body_and_headers(active, body_size, out);
    out->http_1_1 = true;
    out->verify_peer = true;
    out->verify_host = true;
    out->connect_timeout_s = experiment_connect_timeout_s(active);
    out->total_timeout_s = experiment_total_timeout_s(active);
    return true;
}
