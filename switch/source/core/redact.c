#include "redact.h"

#include <stdbool.h>
#include <string.h>

#define AUTH_NAME "Authorization:"
#define AUTH_REDACTED "Authorization: ***"
#define MASK_LENGTH 3

static void append_bounded(char *out, size_t out_size, const char *text, size_t length)
{
    size_t used = strlen(out);
    if (used + 1 >= out_size) {
        return;
    }
    size_t room = out_size - used - 1;
    size_t count = length < room ? length : room;
    memcpy(out + used, text, count);
    out[used + count] = '\0';
}

static char to_lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool starts_with_ignoring_case(const char *text, const char *prefix, size_t prefix_length)
{
    for (size_t i = 0; i < prefix_length; i++) {
        if (to_lower_ascii(text[i]) != to_lower_ascii(prefix[i])) {
            return false;
        }
    }
    return true;
}

static bool is_authorization_line(const char *line, size_t length)
{
    size_t name_length = strlen(AUTH_NAME);
    return length >= name_length && starts_with_ignoring_case(line, AUTH_NAME, name_length);
}

static size_t line_ending_length(const char *line, size_t length)
{
    size_t count = 0;
    while (count < length && (line[length - 1 - count] == '\n' || line[length - 1 - count] == '\r')) {
        count++;
    }
    return count;
}

static void append_redacted_line(char *out, size_t out_size, const char *line, size_t length)
{
    if (!is_authorization_line(line, length)) {
        append_bounded(out, out_size, line, length);
        return;
    }
    size_t ending = line_ending_length(line, length);
    append_bounded(out, out_size, AUTH_REDACTED, strlen(AUTH_REDACTED));
    append_bounded(out, out_size, line + length - ending, ending);
}

void redact_header_line(const char *line, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    append_redacted_line(out, out_size, line, strlen(line));
}

void redact_headers_append(char *out, size_t out_size, const char *text, size_t length)
{
    size_t start = 0;
    while (start < length) {
        size_t end = start;
        while (end < length && text[end] != '\n') {
            end++;
        }
        if (end < length) {
            end++;
        }
        append_redacted_line(out, out_size, text + start, end - start);
        start = end;
    }
}

void redact_secret_inplace(char *text, const char *secret)
{
    size_t secret_length = secret != NULL ? strlen(secret) : 0;
    if (secret_length == 0) {
        return;
    }
    size_t mask_length = secret_length < MASK_LENGTH ? secret_length : MASK_LENGTH;
    const char *read = text;
    char *write = text;
    while (*read != '\0') {
        if (strncmp(read, secret, secret_length) == 0) {
            memset(write, '*', mask_length);
            write += mask_length;
            read += secret_length;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}
