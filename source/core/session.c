#include "session.h"

#include <stdbool.h>
#include <stdio.h>

#include "ts_limits.h"
#include "paths.h"
#include "text_ru.h"
#include "url.h"

static void show_formatted(const upload_port_t *port, const char *format, const char *argument)
{
    char line[TS_LINE_SIZE];
    snprintf(line, sizeof line, format, argument);
    port->show_line(port->ctx, line);
}

static bool file_exists(const char *fs_path)
{
    FILE *file = fopen(fs_path, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

static const char *find_ca_file(const char *sd_root, char *buffer, size_t buffer_size)
{
    paths_fs(sd_root, TS_REL_CA_PEM, buffer, buffer_size);
    return file_exists(buffer) ? buffer : NULL;
}

static void announce_ca(const upload_port_t *port, const char *ca_file)
{
    if (ca_file == NULL) {
        port->show_line(port->ctx, TXT_CA_SYSTEM);
        return;
    }
    char display_path[TS_PATH_SIZE];
    paths_display(TS_REL_CA_PEM, display_path, sizeof display_path);
    show_formatted(port, TXT_CA_USED, display_path);
}

static void show_number_line(const upload_port_t *port, const char *format, int first, int second)
{
    char line[TS_LINE_SIZE];
    snprintf(line, sizeof line, format, first, second);
    port->show_line(port->ctx, line);
}

typedef struct {
    const session_inputs_t *in;
    const upload_port_t *port;
    const body_t *body;
    const char *ca_file;
    const char *variant_tag;
} upload_job_t;

static void show_outcome(const upload_job_t *job, const struct tm *now, int attempt, const upload_outcome_t *outcome)
{
    char line[TS_LINE_SIZE];
    outcome_format_line(now, job->variant_tag, attempt, TS_MAX_ATTEMPTS, job->body->size, outcome, line, sizeof line);
    job->port->show_line(job->port->ctx, line);
    outcome_message(outcome, line, sizeof line);
    job->port->show_line(job->port->ctx, line);
}

static void log_attempt(const upload_job_t *job, const struct tm *now, const upload_request_t *request, int attempt, const upload_outcome_t *outcome)
{
    log_sink_t *log = job->in->log;
    if (log == NULL) {
        return;
    }
    const config_t *config = job->in->config;
    log_append_attempt(log, now, job->variant_tag, request->url, job->body->size, attempt, TS_MAX_ATTEMPTS, outcome, config->username, config->password);
    if (log_take_warning(log)) {
        job->port->show_line(job->port->ctx, TXT_LOG_UNAVAILABLE);
    }
}

static bool build_request(const upload_job_t *job, struct tm *now, upload_request_t *request)
{
    char remote_name[64];
    job->port->now_local(job->port->ctx, now);
    url_format_remote_name(now, remote_name, sizeof remote_name);
    return request_build_put(job->in->config, remote_name, job->body->size, job->ca_file, job->in->experiment, request);
}

static outcome_kind_t run_attempt(const upload_job_t *job, int attempt)
{
    struct tm now;
    upload_request_t request;
    upload_outcome_t outcome = {0};

    if (!build_request(job, &now, &request)) {
        job->port->show_line(job->port->ctx, TXT_REQUEST_BUILD_FAILED);
        return OUTCOME_PREFLIGHT_FAILED;
    }
    show_number_line(job->port, TXT_UPLOADING, attempt, TS_MAX_ATTEMPTS);
    job->port->perform(job->port->ctx, &request, job->body, &outcome);
    show_outcome(job, &now, attempt, &outcome);
    log_attempt(job, &now, &request, attempt, &outcome);
    return outcome_classify(&outcome);
}

static outcome_kind_t upload_body(const session_inputs_t *in, const upload_port_t *port, const body_t *body)
{
    char ca_path[TS_PATH_SIZE];
    char tag[TS_TAG_SIZE];
    const char *ca_file = find_ca_file(in->sd_root, ca_path, sizeof ca_path);
    outcome_kind_t kind;
    int attempt = 0;

    if (in->experiment != NULL) {
        experiment_tag(in->experiment, tag, sizeof tag);
    }
    upload_job_t job = { in, port, body, ca_file, in->experiment != NULL ? tag : NULL };
    announce_ca(port, ca_file);
    do {
        attempt++;
        kind = run_attempt(&job, attempt);
        if (!outcome_should_retry(kind, attempt, TS_MAX_ATTEMPTS)) {
            break;
        }
        show_number_line(port, TXT_RETRYING, TS_RETRY_DELAY_S, 0);
        port->sleep_seconds(port->ctx, TS_RETRY_DELAY_S);
    } while (true);
    return kind;
}

static void show_preflight_failure(const upload_port_t *port, body_status_t status, const body_t *body)
{
    char display_path[TS_PATH_SIZE];
    char line[TS_LINE_SIZE];
    paths_display(TS_REL_TEST_ZIP, display_path, sizeof display_path);
    body_status_message(status, display_path, body->size, TS_MAX_BODY_BYTES, line, sizeof line);
    port->show_line(port->ctx, line);
}

outcome_kind_t session_upload_test_file(const session_inputs_t *in, const upload_port_t *port)
{
    char fs_path[TS_PATH_SIZE];
    body_t body;

    paths_fs(in->sd_root, TS_REL_TEST_ZIP, fs_path, sizeof fs_path);
    body_status_t status = body_load(fs_path, TS_MAX_BODY_BYTES, &body);
    if (status != BODY_OK) {
        show_preflight_failure(port, status, &body);
        return OUTCOME_PREFLIGHT_FAILED;
    }
    outcome_kind_t kind = upload_body(in, port, &body);
    body_free(&body);
    return kind;
}
