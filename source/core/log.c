#include "log.h"

#include <stdio.h>
#include <string.h>

#include "fs_compat.h"
#include "paths.h"
#include "redact.h"

void log_sink_init(log_sink_t *log, const char *sd_root)
{
    memset(log, 0, sizeof *log);
    paths_fs(sd_root, TS_REL_LOG_DIR, log->dir_path, sizeof log->dir_path);
    paths_fs(sd_root, TS_REL_LOG, log->file_path, sizeof log->file_path);
    log->sd_root_length = strlen(sd_root);
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

static void format_entry(char *entry, size_t size, const struct tm *t, const char *variant_tag, const char *url,
                         int64_t body_size, int attempt, int max_attempts, const upload_outcome_t *o)
{
    char summary[TS_LINE_SIZE];
    outcome_format_line(t, variant_tag, attempt, max_attempts, body_size, o, summary, sizeof summary);
    snprintf(entry, size, "%s\nURL: %s\n--- headers ---\n", summary, url);
    redact_headers_append(entry, size, o->headers, strlen(o->headers));
    size_t used = strlen(entry);
    snprintf(entry + used, size - used, "\n\n");
}

bool log_append_attempt(log_sink_t *log, const struct tm *t, const char *variant_tag, const char *url,
                        int64_t body_size, int attempt, int max_attempts, const upload_outcome_t *o,
                        const char *username, const char *password)
{
    char entry[TS_LOG_ENTRY_SIZE];
    format_entry(entry, sizeof entry, t, variant_tag, url, body_size, attempt, max_attempts, o);
    redact_basic_credentials_inplace(entry, username, password);
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
