#include "test_sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t ROUND_CONSTANTS[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotate_right(uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static void process_block(uint32_t state[8], const unsigned char block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8
               | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t v[8];
    memcpy(v, state, sizeof v);
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = rotate_right(v[4], 6) ^ rotate_right(v[4], 11) ^ rotate_right(v[4], 25);
        uint32_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t temp1 = v[7] + s1 + choice + ROUND_CONSTANTS[i] + w[i];
        uint32_t s0 = rotate_right(v[0], 2) ^ rotate_right(v[0], 13) ^ rotate_right(v[0], 22);
        uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        uint32_t temp2 = s0 + majority;
        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += temp1;
        v[0] = temp1 + temp2;
    }
    for (int i = 0; i < 8; i++) {
        state[i] += v[i];
    }
}

void test_sha256_hex(const void *data, size_t size, char out[65])
{
    uint32_t state[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    size_t padded_size = ((size + 8) / 64 + 1) * 64;
    unsigned char *padded = calloc(padded_size, 1);
    if (size > 0) {
        memcpy(padded, data, size);
    }
    padded[size] = 0x80;
    uint64_t bit_length = (uint64_t)size * 8;
    for (int i = 0; i < 8; i++) {
        padded[padded_size - 1 - (size_t)i] = (unsigned char)(bit_length >> (8 * i));
    }
    for (size_t offset = 0; offset < padded_size; offset += 64) {
        process_block(state, padded + offset);
    }
    for (int i = 0; i < 8; i++) {
        snprintf(out + i * 8, 9, "%08x", (unsigned)state[i]);
    }
    free(padded);
}
