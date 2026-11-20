#ifndef TS_TEST_SHA256_H
#define TS_TEST_SHA256_H

#include <stddef.h>

/* Test-only SHA-256 (the app never hashes on the Switch before Phase 3). out: 64 lowercase hex characters and NUL. */
void test_sha256_hex(const void *data, size_t size, char out[65]);

#endif
