#include <string.h>

#include "redact.h"
#include "test.h"

#define PASSWORD "TopSecretAppPassword"
#define BASIC_B64 "TE9HSU46VG9wU2VjcmV0QXBwUGFzc3dvcmQ="

static void redacts_authorization_line_and_keeps_crlf(void)
{
    char out[128];
    redact_header_line("Authorization: Basic " BASIC_B64 "\r\n", out, sizeof out);
    CHECK_STR_EQ(out, "Authorization: ***\r\n");
}

static void redacts_authorization_line_without_line_ending(void)
{
    char out[128];
    redact_header_line("Authorization: Basic abc", out, sizeof out);
    CHECK_STR_EQ(out, "Authorization: ***");
}

static void redacts_header_name_in_any_letter_case(void)
{
    char lower[128];
    char upper[128];
    redact_header_line("authorization: Basic abc\r\n", lower, sizeof lower);
    redact_header_line("AUTHORIZATION: Basic abc\r\n", upper, sizeof upper);
    CHECK_STR_EQ(lower, "Authorization: ***\r\n");
    CHECK_STR_EQ(upper, "Authorization: ***\r\n");
}

static void leaves_other_headers_untouched(void)
{
    char out[128];
    redact_header_line("Content-Length: 5\r\n", out, sizeof out);
    CHECK_STR_EQ(out, "Content-Length: 5\r\n");
}

static void does_not_treat_header_merely_containing_authorization_as_secret(void)
{
    char out[128];
    redact_header_line("X-Authorization-Hint: keep\r\n", out, sizeof out);
    CHECK_STR_EQ(out, "X-Authorization-Hint: keep\r\n");
}

static void header_line_output_is_truncated_safely(void)
{
    char out[8];
    redact_header_line("Content-Length: 5\r\n", out, sizeof out);
    CHECK_STR_EQ(out, "Content");
}

static void redacted_authorization_is_truncated_safely_into_small_buffer(void)
{
    char out[10];
    redact_header_line("Authorization: Basic " BASIC_B64 "\r\n", out, sizeof out);
    CHECK_STR_EQ(out, "Authoriza");
}

static void header_line_with_zero_size_buffer_writes_nothing(void)
{
    char out[4] = "xyz";
    redact_header_line("Authorization: Basic abc", out, 0);
    CHECK_STR_EQ(out, "xyz");
}

static void redacts_authorization_in_the_middle_of_a_header_block(void)
{
    const char *block = "PUT /a HTTP/1.1\r\nHost: h\r\nAuthorization: Basic " BASIC_B64 "\r\nContent-Length: 5\r\n\r\n";
    char out[256] = "";
    redact_headers_append(out, sizeof out, block, strlen(block));
    CHECK_STR_EQ(out, "PUT /a HTTP/1.1\r\nHost: h\r\nAuthorization: ***\r\nContent-Length: 5\r\n\r\n");
}

static void redacts_every_authorization_line_in_a_block(void)
{
    const char *block = "Authorization: Basic one\r\nX: y\r\nauthorization: Basic two\r\n";
    char out[256] = "";
    redact_headers_append(out, sizeof out, block, strlen(block));
    CHECK_STR_EQ(out, "Authorization: ***\r\nX: y\r\nAuthorization: ***\r\n");
}

static void append_honours_length_for_text_without_terminator(void)
{
    const char text[] = {'A', 'B', 'C', 'D'};
    char out[16] = "";
    redact_headers_append(out, sizeof out, text, 2);
    CHECK_STR_EQ(out, "AB");
}

static void append_adds_to_existing_content(void)
{
    char out[64] = "first\r\n";
    redact_headers_append(out, sizeof out, "second\r\n", 8);
    CHECK_STR_EQ(out, "first\r\nsecond\r\n");
}

static void append_keeps_chunks_that_end_on_line_boundaries_redacted(void)
{
    char out[256] = "";
    const char *first = "Host: h\r\nAuthorization: Basic " BASIC_B64 "\r\n";
    const char *second = "Content-Length: 5\r\n";
    redact_headers_append(out, sizeof out, first, strlen(first));
    redact_headers_append(out, sizeof out, second, strlen(second));
    CHECK_STR_EQ(out, "Host: h\r\nAuthorization: ***\r\nContent-Length: 5\r\n");
}

static void second_pass_over_whole_block_hides_authorization_split_mid_value(void)
{
    /* curl may hand a block over in pieces. A single pass cannot recognise the continuation
       piece, so the stored text keeps it; the log re-redacts the concatenated block. */
    char stored[256] = "";
    char logged[256] = "";
    redact_headers_append(stored, sizeof stored, "Authorization: Basic TE9H", 25);
    redact_headers_append(stored, sizeof stored, "SU46cGFzcw==\r\nHost: h\r\n", 23);
    redact_headers_append(logged, sizeof logged, stored, strlen(stored));
    CHECK_STR_EQ(logged, "Authorization: ***\r\nHost: h\r\n");
}

static void second_pass_over_whole_block_hides_authorization_split_inside_header_name(void)
{
    char stored[256] = "";
    char logged[256] = "";
    redact_headers_append(stored, sizeof stored, "Authoriz", 8);
    redact_headers_append(stored, sizeof stored, "ation: Basic TE9HSU46cGFzcw==\r\n", 31);
    redact_headers_append(logged, sizeof logged, stored, strlen(stored));
    CHECK_STR_EQ(logged, "Authorization: ***\r\n");
}

static void append_truncates_at_buffer_size_without_overflow(void)
{
    char out[12] = "";
    const char *block = "Host: example\r\nAuthorization: Basic abc\r\n";
    redact_headers_append(out, sizeof out, block, strlen(block));
    CHECK_STR_EQ(out, "Host: examp");
}

static void append_to_full_buffer_changes_nothing(void)
{
    char out[4] = "abc";
    redact_headers_append(out, sizeof out, "more", 4);
    CHECK_STR_EQ(out, "abc");
}

static void secret_inplace_replaces_every_occurrence(void)
{
    char text[128] = "pw=" PASSWORD " again " PASSWORD "!";
    redact_secret_inplace(text, PASSWORD);
    CHECK_STR_EQ(text, "pw=*** again ***!");
}

static void secret_inplace_leaves_text_without_secret_untouched(void)
{
    char text[64] = "nothing here";
    redact_secret_inplace(text, PASSWORD);
    CHECK_STR_EQ(text, "nothing here");
}

static void secret_inplace_handles_adjacent_occurrences(void)
{
    char text[64] = "abcabc";
    redact_secret_inplace(text, "abc");
    CHECK_STR_EQ(text, "******");
}

static void secret_inplace_ignores_null_secret(void)
{
    char text[64] = "keep me";
    redact_secret_inplace(text, NULL);
    CHECK_STR_EQ(text, "keep me");
}

static void secret_inplace_ignores_empty_secret(void)
{
    char text[64] = "keep me";
    redact_secret_inplace(text, "");
    CHECK_STR_EQ(text, "keep me");
}

static void secret_inplace_masks_short_secret_with_as_many_stars_as_characters(void)
{
    char one[32] = "a-b-a";
    char two[32] = "xabx";
    redact_secret_inplace(one, "a");
    redact_secret_inplace(two, "ab");
    CHECK_STR_EQ(one, "*-b-*");
    CHECK_STR_EQ(two, "x**x");
}

void run_redact_tests(void)
{
    RUN_TEST(redacts_authorization_line_and_keeps_crlf);
    RUN_TEST(redacts_authorization_line_without_line_ending);
    RUN_TEST(redacts_header_name_in_any_letter_case);
    RUN_TEST(leaves_other_headers_untouched);
    RUN_TEST(does_not_treat_header_merely_containing_authorization_as_secret);
    RUN_TEST(header_line_output_is_truncated_safely);
    RUN_TEST(redacted_authorization_is_truncated_safely_into_small_buffer);
    RUN_TEST(header_line_with_zero_size_buffer_writes_nothing);
    RUN_TEST(redacts_authorization_in_the_middle_of_a_header_block);
    RUN_TEST(redacts_every_authorization_line_in_a_block);
    RUN_TEST(append_honours_length_for_text_without_terminator);
    RUN_TEST(append_adds_to_existing_content);
    RUN_TEST(append_keeps_chunks_that_end_on_line_boundaries_redacted);
    RUN_TEST(second_pass_over_whole_block_hides_authorization_split_mid_value);
    RUN_TEST(second_pass_over_whole_block_hides_authorization_split_inside_header_name);
    RUN_TEST(append_truncates_at_buffer_size_without_overflow);
    RUN_TEST(append_to_full_buffer_changes_nothing);
    RUN_TEST(secret_inplace_replaces_every_occurrence);
    RUN_TEST(secret_inplace_leaves_text_without_secret_untouched);
    RUN_TEST(secret_inplace_handles_adjacent_occurrences);
    RUN_TEST(secret_inplace_ignores_null_secret);
    RUN_TEST(secret_inplace_ignores_empty_secret);
    RUN_TEST(secret_inplace_masks_short_secret_with_as_many_stars_as_characters);
}
