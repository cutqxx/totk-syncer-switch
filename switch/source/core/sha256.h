#ifndef TS_SHA256_H
#define TS_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE 65 /* 64 lowercase hex characters and NUL */

/* Streaming SHA-256 (FIPS 180-4): init, any number of updates, final. */
typedef struct {
    uint32_t state[8];
    uint64_t total_bytes;
    unsigned char block[64];
    size_t block_used;
} sha256_t;

void sha256_init(sha256_t *hash);
void sha256_update(sha256_t *hash, const void *data, size_t size);
void sha256_final(sha256_t *hash, unsigned char digest[SHA256_DIGEST_SIZE]);

/* Lowercase hex of a digest. */
void sha256_to_hex(const unsigned char digest[SHA256_DIGEST_SIZE], char out[SHA256_HEX_SIZE]);

/* One-shot hash of a buffer as lowercase hex. */
void sha256_hex(const void *data, size_t size, char out[SHA256_HEX_SIZE]);

#endif
