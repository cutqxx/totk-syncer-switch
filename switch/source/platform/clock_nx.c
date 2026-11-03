#include "clock_nx.h"

#include <string.h>
#include <switch.h>

static bool read_calendar_time(TimeCalendarTime *calendar)
{
    u64 posix_time = 0;
    TimeCalendarAdditionalInfo info;
    if (R_FAILED(timeGetCurrentTime(TimeType_Default, &posix_time))) {
        return false;
    }
    return R_SUCCEEDED(timeToCalendarTimeWithMyRule(posix_time, calendar, &info));
}

static void fill_from_newlib_time(struct tm *out)
{
    time_t now = time(NULL);
    localtime_r(&now, out);
}

void clock_now_local(void *ctx, struct tm *out)
{
    (void)ctx;
    TimeCalendarTime calendar;
    if (!read_calendar_time(&calendar)) {
        fill_from_newlib_time(out);
        return;
    }
    memset(out, 0, sizeof *out);
    out->tm_year = calendar.year - 1900;
    out->tm_mon = calendar.month - 1;
    out->tm_mday = calendar.day;
    out->tm_hour = calendar.hour;
    out->tm_min = calendar.minute;
    out->tm_sec = calendar.second;
}

void clock_sleep_seconds(void *ctx, int seconds)
{
    (void)ctx;
    svcSleepThread((s64)seconds * 1000000000LL);
}
