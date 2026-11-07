#ifndef TS_REDACT_H
#define TS_REDACT_H

#include <stddef.h>

/* Any "Authorization:" header (name matched case-insensitively) becomes
   "Authorization: ***"; a trailing line ending is kept. Other lines are copied. */
void redact_header_line(const char *line, char *out, size_t out_size);

/* Appends text (not necessarily NUL-terminated, may hold many lines) to the
   NUL-terminated out buffer with every Authorization header redacted. Truncates safely. */
void redact_headers_append(char *out, size_t out_size, const char *text, size_t length);

/* Replaces every occurrence of secret with "***" (with as many '*' as the
   secret has characters when it is shorter than 3). No-op for NULL or empty secret. */
void redact_secret_inplace(char *text, const char *secret);

#endif
