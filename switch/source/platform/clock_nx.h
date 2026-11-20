#ifndef TS_CLOCK_NX_H
#define TS_CLOCK_NX_H

#include <time.h>

void clock_now_local(void *ctx, struct tm *out);
void clock_sleep_seconds(void *ctx, int seconds);

#endif
