#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "test.h"

#define TWO_BLOCKS "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"

static void sha256_matches_the_published_vectors(void)
{
    char hash[SHA256_HEX_SIZE];
    sha256_hex("", 0, hash);
    CHECK_STR_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256_hex("abc", 3, hash);
    CHECK_STR_EQ(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256_hex(TWO_BLOCKS, strlen(TWO_BLOCKS), hash);
    CHECK_STR_EQ(hash, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void a_million_a_streamed_in_odd_pieces_matches_the_published_vector(void)
{
    char block[1024]; /* pieces go up to 1009 bytes */
    memset(block, 'a', sizeof block);
    sha256_t hash;
    sha256_init(&hash);
    size_t left = 1000000;
    size_t piece = 1;
    while (left > 0) {
        size_t take = piece < left ? piece : left;
        sha256_update(&hash, block, take);
        left -= take;
        piece = piece % 997 + 13; /* sizes cross every block boundary */
    }
    unsigned char digest[SHA256_DIGEST_SIZE];
    char hex[SHA256_HEX_SIZE];
    sha256_final(&hash, digest);
    sha256_to_hex(digest, hex);
    CHECK_STR_EQ(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void every_split_of_a_message_gives_the_same_hash(void)
{
    /* Lengths around the 56-byte padding edge and the 64-byte block edge. */
    unsigned char message[130];
    for (size_t i = 0; i < sizeof message; i++) {
        message[i] = (unsigned char)(i * 7 + 3);
    }
    for (size_t length = 54; length <= 130; length++) {
        char whole[SHA256_HEX_SIZE];
        sha256_hex(message, length, whole);
        for (size_t split = 0; split <= length; split += 9) {
            sha256_t hash;
            unsigned char digest[SHA256_DIGEST_SIZE];
            char parts[SHA256_HEX_SIZE];
            sha256_init(&hash);
            sha256_update(&hash, message, split);
            sha256_update(&hash, message + split, length - split);
            sha256_final(&hash, digest);
            sha256_to_hex(digest, parts);
            CHECK_STR_EQ(parts, whole);
        }
    }
}

void run_sha256_tests(void)
{
    RUN_TEST(sha256_matches_the_published_vectors);
    RUN_TEST(a_million_a_streamed_in_odd_pieces_matches_the_published_vector);
    RUN_TEST(every_split_of_a_message_gives_the_same_hash);
}
