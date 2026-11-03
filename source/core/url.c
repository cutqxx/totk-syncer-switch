#include "url.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool is_ok;
} url_buffer_t;

static void buffer_init(url_buffer_t *buf, char *data, size_t capacity)
{
    buf->data = data;
    buf->capacity = capacity;
    buf->length = 0;
    buf->is_ok = capacity > 0;
    if (buf->is_ok) {
        data[0] = '\0';
    }
}

static void buffer_append(url_buffer_t *buf, const char *text, size_t count)
{
    if (!buf->is_ok || buf->length + count + 1 > buf->capacity) {
        buf->is_ok = false;
        return;
    }
    memcpy(buf->data + buf->length, text, count);
    buf->length += count;
    buf->data[buf->length] = '\0';
}

static bool is_unreserved(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

static void buffer_append_encoded(url_buffer_t *buf, const char *segment, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        unsigned char c = (unsigned char)segment[i];
        char encoded[4];
        if (is_unreserved(c)) {
            buffer_append(buf, segment + i, 1);
        } else {
            snprintf(encoded, sizeof encoded, "%%%02X", c);
            buffer_append(buf, encoded, 3);
        }
    }
}

static void buffer_append_path_segment(url_buffer_t *buf, const char *segment, size_t count)
{
    buffer_append(buf, "/", 1);
    buffer_append_encoded(buf, segment, count);
}

static void buffer_append_path(url_buffer_t *buf, const char *path)
{
    while (*path != '\0') {
        size_t length = strcspn(path, "/");
        if (length > 0) {
            buffer_append_path_segment(buf, path, length);
        }
        path += length;
        if (*path == '/') {
            path++;
        }
    }
}

static size_t length_without_trailing_slashes(const char *text)
{
    size_t length = strlen(text);
    while (length > 0 && text[length - 1] == '/') {
        length--;
    }
    return length;
}

bool url_percent_encode_segment(const char *segment, char *out, size_t out_size)
{
    url_buffer_t buf;
    buffer_init(&buf, out, out_size);
    buffer_append_encoded(&buf, segment, strlen(segment));
    return buf.is_ok;
}

bool url_build_destination(const char *origin, const char *basepath, const char *filename, char *out, size_t out_size)
{
    url_buffer_t buf;
    buffer_init(&buf, out, out_size);
    buffer_append(&buf, origin, length_without_trailing_slashes(origin));
    buffer_append_path(&buf, basepath);
    buffer_append_path(&buf, filename);
    return buf.is_ok;
}

void url_format_remote_name(const struct tm *t, char *out, size_t out_size)
{
    snprintf(out, out_size, "spike_%04d%02d%02d-%02d%02d%02d.zip",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
}
