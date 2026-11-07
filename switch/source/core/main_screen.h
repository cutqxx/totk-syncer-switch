#ifndef TS_MAIN_SCREEN_H
#define TS_MAIN_SCREEN_H

#include <stddef.h>

#include "config.h"
#include "pc_status.h"

#define MAIN_SCREEN_SIZE 1024

/* Builds the whole main screen as ASCII text; the token is never part of it.
   cfg == NULL: only blocking_error and the exit hint (no network actions are offered).
   status == NULL: the status line reads "checking". */
void main_screen_render(const config_t *cfg, const char *blocking_error, const pc_status_t *status, char *out, size_t out_size);

#endif
