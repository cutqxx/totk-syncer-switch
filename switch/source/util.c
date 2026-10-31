#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __SWITCH__
#include <switch.h>
#else
#include <unistd.h>
#endif

void buf_free(Buffer *b) {
    free(b->data);
    b->data = NULL;
    b->size = b->cap = 0;
}

bool buf_reserve(Buffer *b, size_t cap) {
    if (cap <= b->cap) return true;
    size_t ncap = b->cap ? b->cap : 4096;
    while (ncap < cap) ncap *= 2;
    uint8_t *p = realloc(b->data, ncap);
    if (!p) return false;
    b->data = p;
    b->cap = ncap;
    return true;
}

bool buf_append(Buffer *b, const void *data, size_t len) {
    // Keep one spare byte so text responses can be NUL-terminated.
    if (!buf_reserve(b, b->size + len + 1)) return false;
    if (len) memcpy(b->data + b->size, data, len);
    b->size += len;
    b->data[b->size] = 0;
    return true;
}

bool str_copy(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0) return false;
    size_t n = strlen(src);
    if (n >= dst_size) {
        memcpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = 0;
        return false;
    }
    memcpy(dst, src, n + 1);
    return true;
}

bool str_starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool str_ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && memcmp(s + a - b, suffix, b) == 0;
}

static bool is_unreserved(unsigned char c) {
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

bool url_encode_path(char *dst, size_t dst_size, const char *path) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        if (is_unreserved(*p) || *p == '/') {
            if (o + 1 >= dst_size) return false;
            dst[o++] = (char)*p;
        } else {
            if (o + 3 >= dst_size) return false;
            dst[o++] = '%';
            dst[o++] = hex[*p >> 4];
            dst[o++] = hex[*p & 15];
        }
    }
    if (o >= dst_size) return false;
    dst[o] = 0;
    return true;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void url_decode_inplace(char *s) {
    char *o = s;
    for (char *p = s; *p; p++) {
        int h, l;
        if (*p == '%' && (h = hexval(p[1])) >= 0 && (l = hexval(p[2])) >= 0) {
            *o++ = (char)(h * 16 + l);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = 0;
}

time_t utc_mktime(int year, int mon, int day, int hour, int min, int sec) {
    // Days from civil (Howard Hinnant's algorithm).
    int y = year - (mon <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = (long long)era * 146097 + (long long)doe - 719468;
    return (time_t)(days * 86400 + hour * 3600 + min * 60 + sec);
}

bool parse_http_date(const char *s, time_t *out) {
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char wday[8] = {0}, mon[8] = {0};
    int day, year, h, m, sec;
    while (*s && isspace((unsigned char)*s)) s++;
    if (sscanf(s, "%7[A-Za-z], %d %7s %d %d:%d:%d", wday, &day, mon, &year, &h, &m, &sec) != 7)
        return false;
    for (int i = 0; i < 12; i++) {
        if (strcmp(mon, months[i]) == 0) {
            if (day < 1 || day > 31 || h > 23 || m > 59 || sec > 60) return false;
            *out = utc_mktime(year, i + 1, day, h, m, sec);
            return true;
        }
    }
    return false;
}

void local_time(time_t t, struct tm *out) {
#ifdef __SWITCH__
    TimeCalendarTime cal;
    TimeCalendarAdditionalInfo info;
    if (R_SUCCEEDED(timeToCalendarTimeWithMyRule((u64)t, &cal, &info))) {
        memset(out, 0, sizeof(*out));
        out->tm_year = cal.year - 1900;
        out->tm_mon = cal.month - 1;
        out->tm_mday = cal.day;
        out->tm_hour = cal.hour;
        out->tm_min = cal.minute;
        out->tm_sec = cal.second;
        return;
    }
    gmtime_r(&t, out);
#else
    localtime_r(&t, out);
#endif
}

void timestamp_now(char *dst, size_t dst_size) {
    struct tm tm;
    local_time(time(NULL), &tm);
    snprintf(dst, dst_size, "%04d-%02d-%02d_%02d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

bool mkdirs(const char *path) {
    char tmp[512];
    if (!str_copy(tmp, sizeof tmp, path)) return false;
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = 0;
    // Skip the device prefix ("sdmc:/") or a leading '/'.
    char *start = strchr(tmp, ':');
    start = start ? start + 1 : tmp;
    while (*start == '/') start++;
    for (char *p = start; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return false;
    return true;
}

bool file_read_all(const char *path, Buffer *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t chunk[16384];
    size_t n;
    bool ok = true;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (!buf_append(out, chunk, n)) {
            ok = false;
            break;
        }
    }
    if (ferror(f)) ok = false;
    fclose(f);
    return ok;
}

bool file_write_atomic(const char *path, const void *data, size_t len) {
    char part[600];
    snprintf(part, sizeof part, "%s.part", path);
    FILE *f = fopen(part, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (ok) {
        remove(path);
        ok = rename(part, path) == 0;
    }
    if (!ok) remove(part);
    return ok;
}

bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

void sleep_ms(unsigned ms) {
#ifdef __SWITCH__
    svcSleepThread((s64)ms * 1000000LL);
#else
    usleep(ms * 1000);
#endif
}

double now_seconds(void) {
#ifdef __SWITCH__
    return (double)armTicksToNs(armGetSystemTick()) / 1e9;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}
