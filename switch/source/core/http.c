#include "http.h"

#include <stdio.h>
#include <string.h>

void http_request_init(http_request_t *req, http_method_t method)
{
    memset(req, 0, sizeof *req);
    req->method = method;
    req->max_response_bytes = TS_MAX_JSON_BYTES;
    req->connect_timeout_s = TS_CONNECT_TIMEOUT_S;
    req->total_timeout_s = TS_TOTAL_TIMEOUT_S;
}

void http_request_add_header(http_request_t *req, const char *line)
{
    if (req->header_count >= TS_MAX_HEADERS) {
        return;
    }
    snprintf(req->headers[req->header_count], sizeof req->headers[0], "%s", line);
    req->header_count++;
}

const char *http_method_name(http_method_t method)
{
    switch (method) {
    case HTTP_PUT: return "PUT";
    case HTTP_POST: return "POST";
    default: return "GET";
    }
}

void http_response_init(http_response_t *resp)
{
    memset(resp, 0, sizeof *resp);
}

void http_response_free(http_response_t *resp)
{
    body_free(&resp->body);
}

static char lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool line_starts_with_name(const char *line, const char *name, size_t name_length)
{
    for (size_t i = 0; i < name_length; i++) {
        if (line[i] == '\0' || lower_ascii(line[i]) != lower_ascii(name[i])) {
            return false;
        }
    }
    return line[name_length] == ':';
}

static bool is_blank_char(char c)
{
    return c == ' ' || c == '\t';
}

static bool line_value_equals(const char *line_after_colon, const char *value)
{
    const char *start = line_after_colon;
    while (is_blank_char(*start)) {
        start++;
    }
    size_t length = strcspn(start, "\r\n");
    while (length > 0 && is_blank_char(start[length - 1])) {
        length--;
    }
    return length == strlen(value) && strncmp(start, value, length) == 0;
}

bool http_response_has_header(const http_response_t *resp, const char *name, const char *value)
{
    size_t name_length = strlen(name);
    const char *line = resp->response_headers;
    while (*line != '\0') {
        if (line_starts_with_name(line, name, name_length) && line_value_equals(line + name_length + 1, value)) {
            return true;
        }
        const char *next = strchr(line, '\n');
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }
    return false;
}

static void copy_trimmed_value(const char *line_after_colon, char *out, size_t out_size)
{
    const char *start = line_after_colon;
    while (is_blank_char(*start)) {
        start++;
    }
    size_t length = strcspn(start, "\r\n");
    while (length > 0 && is_blank_char(start[length - 1])) {
        length--;
    }
    snprintf(out, out_size, "%.*s", (int)length, start);
}

bool http_response_header_value(const http_response_t *resp, const char *name, char *out, size_t out_size)
{
    size_t name_length = strlen(name);
    bool is_found = false;
    out[0] = '\0';
    for (const char *line = resp->response_headers; *line != '\0';) {
        if (line_starts_with_name(line, name, name_length)) {
            copy_trimmed_value(line + name_length + 1, out, out_size);
            is_found = true;
        }
        const char *next = strchr(line, '\n');
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }
    return is_found;
}
