#include <stdlib.h>
#include <string.h>

#include "body.h"
#include "test.h"

static void append_stores_data_followed_by_nul(void)
{
    body_t body = { 0 };
    CHECK(body_append(&body, "abc", 3, 100));
    CHECK_INT_EQ(body.size, 3);
    CHECK(memcmp(body.data, "abc\0", 4) == 0);
    body_free(&body);
}

static void append_concatenates_chunks_and_keeps_nul_last(void)
{
    body_t body = { 0 };
    CHECK(body_append(&body, "ab", 2, 100));
    CHECK(body_append(&body, "cde", 3, 100));
    CHECK_INT_EQ(body.size, 5);
    CHECK_STR_EQ((const char *)body.data, "abcde");
    body_free(&body);
}

static void append_accepts_data_that_exactly_reaches_the_limit(void)
{
    body_t body = { 0 };
    CHECK(body_append(&body, "abcd", 4, 4));
    CHECK_INT_EQ(body.size, 4);
    CHECK_INT_EQ(body.data[4], 0);
    body_free(&body);
}

static void append_rejects_data_over_the_limit_and_keeps_body_unchanged(void)
{
    body_t body = { 0 };
    CHECK(body_append(&body, "abc", 3, 4));
    CHECK(!body_append(&body, "de", 2, 4));
    CHECK_INT_EQ(body.size, 3);
    CHECK_STR_EQ((const char *)body.data, "abc");
    body_free(&body);
}

static void append_rejects_first_chunk_larger_than_limit(void)
{
    body_t body = { 0 };
    CHECK(!body_append(&body, "abcde", 5, 4));
    CHECK(body.data == NULL);
    CHECK_INT_EQ(body.size, 0);
}

static void append_of_zero_bytes_succeeds_and_leaves_body_empty(void)
{
    body_t body = { 0 };
    CHECK(body_append(&body, "", 0, 4));
    CHECK(body.data == NULL);
    CHECK_INT_EQ(body.size, 0);
}

static void free_resets_body_to_empty(void)
{
    body_t body = { 0 };
    body_append(&body, "abc", 3, 10);
    body_free(&body);
    CHECK(body.data == NULL);
    CHECK_INT_EQ(body.size, 0);
}

static void adopt_takes_over_the_buffer_and_terminates_it(void)
{
    body_t body = { NULL, 0 };
    unsigned char *heap = malloc(4);
    memcpy(heap, "PK\x03\x04", 4);
    CHECK(body_adopt(&body, heap, 4));
    CHECK_INT_EQ(body.size, 4);
    CHECK(memcmp(body.data, "PK\x03\x04", 4) == 0);
    CHECK_INT_EQ(body.data[4], 0);
    body_free(&body);
}

static void adopt_accepts_an_empty_buffer(void)
{
    body_t body = { NULL, 0 };
    CHECK(body_adopt(&body, malloc(1), 0));
    CHECK_INT_EQ(body.size, 0);
    CHECK(body.data != NULL);
    CHECK_INT_EQ(body.data[0], 0);
    body_free(&body);
}

static void an_adopted_body_can_be_appended_to_and_freed(void)
{
    body_t body = { NULL, 0 };
    unsigned char *heap = malloc(3);
    memcpy(heap, "abc", 3);
    CHECK(body_adopt(&body, heap, 3));
    CHECK(body_append(&body, "def", 3, 100));
    CHECK_INT_EQ(body.size, 6);
    CHECK_STR_EQ((const char *)body.data, "abcdef");
    body_free(&body);
    CHECK(body.data == NULL);
    CHECK_INT_EQ(body.size, 0);
}

void run_body_tests(void)
{
    RUN_TEST(append_stores_data_followed_by_nul);
    RUN_TEST(append_concatenates_chunks_and_keeps_nul_last);
    RUN_TEST(append_accepts_data_that_exactly_reaches_the_limit);
    RUN_TEST(append_rejects_data_over_the_limit_and_keeps_body_unchanged);
    RUN_TEST(append_rejects_first_chunk_larger_than_limit);
    RUN_TEST(append_of_zero_bytes_succeeds_and_leaves_body_empty);
    RUN_TEST(free_resets_body_to_empty);
    RUN_TEST(adopt_takes_over_the_buffer_and_terminates_it);
    RUN_TEST(adopt_accepts_an_empty_buffer);
    RUN_TEST(an_adopted_body_can_be_appended_to_and_freed);
}
