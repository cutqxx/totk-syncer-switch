#ifndef TS_REQUEST_H
#define TS_REQUEST_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "experiment.h"

#define TS_REQ_MAX_HEADERS 4

typedef struct {
    char url[1024];
    char ca_file[256];
    const char *username;
    const char *password;
    int64_t content_length;
    const char *headers[TS_REQ_MAX_HEADERS];
    int header_count;
    bool http_1_1;
    bool verify_peer;
    bool verify_host;
    long connect_timeout_s;
    long total_timeout_s;
} upload_request_t;

/* ca_file may be NULL: no custom CA bundle (ca_file[0] == '\0').
   experiment may be NULL: spec 5.4 defaults. content_length == -1 means chunked. */
bool request_build_put(const config_t *cfg, const char *remote_name, int64_t body_size, const char *ca_file,
                       const experiment_t *experiment, upload_request_t *out);

#endif
