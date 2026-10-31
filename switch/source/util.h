// Small platform-neutral helpers: buffers, strings, URL encoding, time, files.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Growable byte buffer.
typedef struct {
    uint8_t *data;
    size_t size;
    size_t cap;
} Buffer;

void buf_free(Buffer *b);
bool buf_reserve(Buffer *b, size_t cap);
bool buf_append(Buffer *b, const void *data, size_t len);

// Safe string copy (always NUL-terminates). Returns false if truncated.
bool str_copy(char *dst, size_t dst_size, const char *src);
bool str_starts_with(const char *s, const char *prefix);
bool str_ends_with(const char *s, const char *suffix);

// Percent-encodes every byte except RFC 3986 unreserved chars and '/'.
// Returns false if dst is too small.
bool url_encode_path(char *dst, size_t dst_size, const char *path);
// Decodes %XX sequences in place.
void url_decode_inplace(char *s);

// Parses an RFC 1123 date ("Sat, 31 Oct 2026 12:34:56 GMT") into UTC epoch.
// Returns false on failure.
bool parse_http_date(const char *s, time_t *out);
// timegm() replacement (newlib on Switch has no timegm).
time_t utc_mktime(int year, int mon /*1-12*/, int day, int hour, int min, int sec);

// Broken-down local time for "t" (uses the console's time zone on Switch).
void local_time(time_t t, struct tm *out);
// Current time as "YYYY-MM-DD_HH-MM-SS" in local time.
void timestamp_now(char *dst, size_t dst_size);

// Filesystem helpers (work for "sdmc:/" and mounted save devices).
bool mkdirs(const char *path);  // like mkdir -p
bool file_read_all(const char *path, Buffer *out);
// Writes via "<path>.part" + rename so readers never see a partial file.
bool file_write_atomic(const char *path, const void *data, size_t len);
bool file_exists(const char *path);

// Sleep for the given number of milliseconds.
void sleep_ms(unsigned ms);
// Monotonic seconds (for measuring durations).
double now_seconds(void);
