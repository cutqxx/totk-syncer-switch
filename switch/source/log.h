// Append-only log file (sd:/switch/totk-sync/log.txt). Never log passwords.
#pragma once

#include <stdarg.h>

void log_open(const char *path);
void log_close(void);
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
