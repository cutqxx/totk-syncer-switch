#ifndef TS_TEST_H
#define TS_TEST_H

#include <stdio.h>
#include <string.h>

extern int ts_checks;
extern int ts_failures;

#define CHECK(cond) \
    do { \
        ts_checks++; \
        if (!(cond)) { \
            ts_failures++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_INT_EQ(actual, expected) \
    do { \
        long long ts_a = (long long)(actual); \
        long long ts_e = (long long)(expected); \
        ts_checks++; \
        if (ts_a != ts_e) { \
            ts_failures++; \
            printf("FAIL %s:%d: %s == %lld (expected %lld)\n", __FILE__, __LINE__, #actual, ts_a, ts_e); \
        } \
    } while (0)

#define CHECK_STR_EQ(actual, expected) \
    do { \
        const char *ts_a = (actual); \
        const char *ts_e = (expected); \
        ts_checks++; \
        if (strcmp(ts_a, ts_e) != 0) { \
            ts_failures++; \
            printf("FAIL %s:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n", __FILE__, __LINE__, #actual, ts_a, ts_e); \
        } \
    } while (0)

#define CHECK_STR_NOT_CONTAINS(haystack, needle) \
    do { \
        const char *ts_h = (haystack); \
        const char *ts_n = (needle); \
        ts_checks++; \
        if (strstr(ts_h, ts_n) != NULL) { \
            ts_failures++; \
            printf("FAIL %s:%d: \"%s\" contains \"%s\"\n", __FILE__, __LINE__, ts_h, ts_n); \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        int ts_failures_before = ts_failures; \
        fn(); \
        printf("%s %s\n", ts_failures == ts_failures_before ? "ok  " : "FAIL", #fn); \
    } while (0)

#endif
