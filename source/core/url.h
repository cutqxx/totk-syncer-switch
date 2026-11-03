#ifndef TS_URL_H
#define TS_URL_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

bool url_percent_encode_segment(const char *segment, char *out, size_t out_size);
bool url_build_destination(const char *origin, const char *basepath, const char *filename, char *out, size_t out_size);
void url_format_remote_name(const struct tm *t, char *out, size_t out_size);

#endif
