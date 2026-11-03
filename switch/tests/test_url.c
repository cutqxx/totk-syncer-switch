#include <string.h>
#include <time.h>

#include "test.h"
#include "url.h"

static struct tm make_time(int year, int month, int day, int hour, int min, int sec)
{
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    return t;
}

static void remote_name_uses_console_clock_fields(void)
{
    struct tm t = make_time(2026, 11, 3, 14, 25, 1);
    char name[64];
    url_format_remote_name(&t, name, sizeof name);
    CHECK_STR_EQ(name, "spike_20261103-142501.zip");
}

static void remote_name_zero_pads_single_digit_fields(void)
{
    struct tm t = make_time(2027, 1, 2, 3, 4, 5);
    char name[64];
    url_format_remote_name(&t, name, sizeof name);
    CHECK_STR_EQ(name, "spike_20270102-030405.zip");
}

static void remote_name_differs_when_clock_differs_by_one_second(void)
{
    struct tm first = make_time(2026, 11, 3, 14, 25, 1);
    struct tm second = make_time(2026, 11, 3, 14, 25, 2);
    char name_a[64], name_b[64];
    url_format_remote_name(&first, name_a, sizeof name_a);
    url_format_remote_name(&second, name_b, sizeof name_b);
    CHECK(strcmp(name_a, name_b) != 0);
}

static void destination_joins_origin_basepath_and_filename_with_single_slashes(void)
{
    char url[256];
    CHECK(url_build_destination("https://webdav.yandex.com", "JKSV", "a.zip", url, sizeof url));
    CHECK_STR_EQ(url, "https://webdav.yandex.com/JKSV/a.zip");
}

static void destination_collapses_extra_and_repeated_slashes_at_joins(void)
{
    char url[256];
    CHECK(url_build_destination("https://h.example///", "//JKSV//sub/", "/a.zip", url, sizeof url));
    CHECK_STR_EQ(url, "https://h.example/JKSV/sub/a.zip");
}

static void destination_encodes_spaces_in_each_segment_but_keeps_separators(void)
{
    char url[256];
    CHECK(url_build_destination("https://h.example", "JKSV/The Legend of Zelda Tears of the Kingdom", "a b.zip", url, sizeof url));
    CHECK_STR_EQ(url, "https://h.example/JKSV/The%20Legend%20of%20Zelda%20Tears%20of%20the%20Kingdom/a%20b.zip");
}

static void destination_keeps_origin_port_and_encodes_percent_in_segments(void)
{
    char url[256];
    CHECK(url_build_destination("https://h.example:8443", "a%b", "c.zip", url, sizeof url));
    CHECK_STR_EQ(url, "https://h.example:8443/a%25b/c.zip");
}

static void destination_percent_encodes_cyrillic_utf8_bytes(void)
{
    char url[256];
    CHECK(url_build_destination("https://h.example", "\xD0\x98\xD0\xB3\xD1\x80\xD0\xB0", "f.zip", url, sizeof url));
    CHECK_STR_EQ(url, "https://h.example/%D0%98%D0%B3%D1%80%D0%B0/f.zip");
}

static void destination_with_empty_basepath_puts_file_under_origin(void)
{
    char url[256];
    CHECK(url_build_destination("https://h.example/", "", "a.zip", url, sizeof url));
    CHECK_STR_EQ(url, "https://h.example/a.zip");
}

static void destination_fails_when_buffer_too_small(void)
{
    char url[16];
    CHECK(!url_build_destination("https://webdav.yandex.com", "JKSV", "a.zip", url, sizeof url));
}

static void destination_fits_exactly_with_terminator_and_fails_one_byte_less(void)
{
    const char *expected = "https://h.example/a.zip";
    char url[32];
    CHECK(url_build_destination("https://h.example", "", "a.zip", url, strlen(expected) + 1));
    CHECK(!url_build_destination("https://h.example", "", "a.zip", url, strlen(expected)));
}

static void encode_segment_keeps_unreserved_characters(void)
{
    char out[64];
    CHECK(url_percent_encode_segment("Az09-._~", out, sizeof out));
    CHECK_STR_EQ(out, "Az09-._~");
}

static void encode_segment_uses_uppercase_hex_for_reserved_characters(void)
{
    char out[64];
    CHECK(url_percent_encode_segment("a b/c?d#e", out, sizeof out));
    CHECK_STR_EQ(out, "a%20b%2Fc%3Fd%23e");
}

static void encode_segment_of_empty_string_is_empty(void)
{
    char out[8] = "x";
    CHECK(url_percent_encode_segment("", out, sizeof out));
    CHECK_STR_EQ(out, "");
}

static void encode_segment_fails_when_buffer_zero_sized(void)
{
    char out[1];
    CHECK(!url_percent_encode_segment("a", out, 0));
}

void run_url_tests(void)
{
    RUN_TEST(remote_name_uses_console_clock_fields);
    RUN_TEST(remote_name_zero_pads_single_digit_fields);
    RUN_TEST(remote_name_differs_when_clock_differs_by_one_second);
    RUN_TEST(destination_joins_origin_basepath_and_filename_with_single_slashes);
    RUN_TEST(destination_collapses_extra_and_repeated_slashes_at_joins);
    RUN_TEST(destination_encodes_spaces_in_each_segment_but_keeps_separators);
    RUN_TEST(destination_keeps_origin_port_and_encodes_percent_in_segments);
    RUN_TEST(destination_percent_encodes_cyrillic_utf8_bytes);
    RUN_TEST(destination_with_empty_basepath_puts_file_under_origin);
    RUN_TEST(destination_fails_when_buffer_too_small);
    RUN_TEST(destination_fits_exactly_with_terminator_and_fails_one_byte_less);
    RUN_TEST(encode_segment_keeps_unreserved_characters);
    RUN_TEST(encode_segment_uses_uppercase_hex_for_reserved_characters);
    RUN_TEST(encode_segment_of_empty_string_is_empty);
    RUN_TEST(encode_segment_fails_when_buffer_zero_sized);
}
