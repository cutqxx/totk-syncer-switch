#include "log.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "fs_compat.h"
#include "paths.h"
#include "redact.h"

void log_sink_init(log_sink_t *log, const char *sd_root, const char *secret)
{
    memset(log, 0, sizeof *log);
    paths_fs(sd_root, TS_REL_LOG_DIR, log->dir_path, sizeof log->dir_path);
    paths_fs(sd_root, TS_REL_LOG, log->file_path, sizeof log->file_path);
    log->sd_root_length = strlen(sd_root);
    if (secret != NULL) {
        snprintf(log->secret, sizeof log->secret, "%s", secret);
    }
}

static void make_log_directories(const log_sink_t *log)
{
    char path[TS_PATH_SIZE];
    snprintf(path, sizeof path, "%s", log->dir_path);
    size_t path_length = strlen(path);
    for (size_t i = log->sd_root_length + 1; i < path_length; i++) {
        if (path[i] == '/') {
            path[i] = '\0';
            ts_mkdir(path);
            path[i] = '/';
        }
    }
    ts_mkdir(path);
}

static bool append_to_file(const log_sink_t *log, const char *entry)
{
    make_log_directories(log);
    FILE *file = fopen(log->file_path, "ab");
    if (file == NULL) {
        return false;
    }
    bool is_written = fputs(entry, file) >= 0;
    return fclose(file) == 0 && is_written;
}

static void format_summary(char *out, size_t size, const struct tm *t, const http_request_t *req,
                           const http_response_t *resp, int attempt, int max_attempts)
{
    snprintf(out, size, "[%04d-%02d-%02d %02d:%02d:%02d] %s attempt %d/%d  HTTP %ld  curl %d (%s)  %.1f s  %" PRId64 " B sent",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec,
             http_method_name(req->method), attempt, max_attempts, resp->http_code, resp->curl_code,
             resp->curl_error, resp->seconds, resp->bytes_sent);
}

static void format_entry(char *entry, size_t size, const struct tm *t, const http_request_t *req,
                         const http_response_t *resp, int attempt, int max_attempts)
{
    char summary[TS_LINE_SIZE];
    format_summary(summary, sizeof summary, t, req, resp, attempt, max_attempts);
    snprintf(entry, size, "%s\nURL: %s\n--- headers ---\n", summary, req->url);
    redact_headers_append(entry, size, resp->trace, strlen(resp->trace));
    size_t used = strlen(entry);
    snprintf(entry + used, size - used, "\n\n");
}

bool log_append_attempt(log_sink_t *log, const struct tm *t, const http_request_t *req, const http_response_t *resp,
                        int attempt, int max_attempts)
{
    char entry[TS_LOG_ENTRY_SIZE];
    format_entry(entry, sizeof entry, t, req, resp, attempt, max_attempts);
    redact_secret_inplace(entry, log->secret);
    bool is_written = append_to_file(log, entry);
    if (!is_written) {
        log->has_failed = true;
    }
    return is_written;
}

bool log_take_warning(log_sink_t *log)
{
    if (!log->has_failed || log->is_warning_shown) {
        return false;
    }
    log->is_warning_shown = true;
    return true;
}
