#include "ascii_text.h"
#include "test.h"

static void printable_ascii_is_copied_unchanged(void)
{
    char out[32];
    CHECK_INT_EQ(ascii_copy("Link ~ 01", out, sizeof out), 9);
    CHECK_STR_EQ(out, "Link ~ 01");
}

static void every_byte_outside_printable_ascii_becomes_a_question_mark(void)
{
    char out[32];
    CHECK_INT_EQ(ascii_copy("a\x7f\x1f\xd0\xa0z\n", out, sizeof out), 7);
    CHECK_STR_EQ(out, "a????z?");
}

static void long_text_is_truncated_and_terminated(void)
{
    char out[5];
    CHECK_INT_EQ(ascii_copy("abcdefgh", out, sizeof out), 4);
    CHECK_STR_EQ(out, "abcd");
}

static void missing_text_gives_an_empty_string(void)
{
    char out[8] = "xxxx";
    CHECK_INT_EQ(ascii_copy(NULL, out, sizeof out), 0);
    CHECK_STR_EQ(out, "");
}

void run_ascii_text_tests(void)
{
    RUN_TEST(printable_ascii_is_copied_unchanged);
    RUN_TEST(every_byte_outside_printable_ascii_becomes_a_question_mark);
    RUN_TEST(long_text_is_truncated_and_terminated);
    RUN_TEST(missing_text_gives_an_empty_string);
}
