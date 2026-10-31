#include "log.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "util.h"

#define LOG_MAX_BYTES (512 * 1024)

static FILE *g_log;

void log_open(const char *path) {
    struct stat st;
    // Simple rotation: start over when the log gets large.
    if (stat(path, &st) == 0 && st.st_size > LOG_MAX_BYTES) {
        char old[600];
        snprintf(old, sizeof old, "%s.old", path);
        remove(old);
        rename(path, old);
    }
    g_log = fopen(path, "a");
}

void log_close(void) {
    if (g_log) fclose(g_log);
    g_log = NULL;
}

void log_msg(const char *fmt, ...) {
    if (!g_log) return;
    char ts[32];
    timestamp_now(ts, sizeof ts);
    fprintf(g_log, "[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}
