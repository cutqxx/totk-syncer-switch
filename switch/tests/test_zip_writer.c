#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "test_zip_read.h"
#include "zip_writer.h"

static struct tm entry_time(void)
{
    struct tm when;
    memset(&when, 0, sizeof when);
    when.tm_year = 126;
    when.tm_mon = 10;
    when.tm_mday = 3;
    when.tm_hour = 14;
    when.tm_min = 25;
    when.tm_sec = 2;
    return when;
}

static void finish_into(zip_writer_t *writer, body_t *zip)
{
    CHECK(zip_writer_finish(writer, zip));
}

static void a_directory_name_gets_a_trailing_slash_exactly_once(void)
{
    struct tm when = entry_time();
    body_t zip = { NULL, 0 };
    test_zip_t listing;
    zip_writer_t *writer = zip_writer_open(&when, 0, 0);
    CHECK(writer != NULL);
    CHECK(zip_writer_add_directory(writer, "slot_00"));
    CHECK(zip_writer_add_directory(writer, "album/"));
    finish_into(writer, &zip);
    bool is_listed = test_zip_open(zip.data, (size_t)zip.size, &listing);
    CHECK(is_listed);
    if (is_listed) {
        CHECK_INT_EQ(listing.count, 2);
        CHECK_STR_EQ(listing.items[0].name, "slot_00/");
        CHECK(listing.items[0].is_directory);
        CHECK_STR_EQ(listing.items[1].name, "album/");
        CHECK(listing.items[1].is_directory);
        CHECK_INT_EQ(listing.items[0].size, 0);
        test_zip_close(&listing);
    }
    body_free(&zip);
}

static void files_come_back_byte_for_byte_including_empty_and_incompressible(void)
{
    struct tm when = entry_time();
    static unsigned char noise[300 * 1024];
    static unsigned char zeros[2 * 1024 * 1024];
    body_t zip = { NULL, 0 };
    test_zip_t listing;
    unsigned int seed = 12345;
    for (size_t i = 0; i < sizeof noise; i++) {
        seed = seed * 1103515245u + 12345u;
        noise[i] = (unsigned char)(seed >> 16);
    }
    memset(zeros, 0, sizeof zeros);
    zip_writer_t *writer = zip_writer_open(&when, 0, sizeof noise + sizeof zeros + 9);
    CHECK(zip_writer_add_file(writer, "empty.sav", "", 0));
    CHECK(zip_writer_add_file(writer, "text.sav", "progress", 8));
    CHECK(zip_writer_add_file(writer, "dir/noise.bin", noise, sizeof noise));
    CHECK(zip_writer_add_file(writer, "dir/zeros.bin", zeros, sizeof zeros));
    finish_into(writer, &zip);
    bool is_listed = test_zip_open(zip.data, (size_t)zip.size, &listing);
    CHECK(is_listed);
    if (is_listed) {
        CHECK_INT_EQ(listing.count, 4);
        CHECK_INT_EQ(test_zip_find(&listing, "empty.sav")->size, 0);
        CHECK(memcmp(test_zip_find(&listing, "text.sav")->data, "progress", 8) == 0);
        CHECK_INT_EQ(test_zip_find(&listing, "dir/noise.bin")->size, sizeof noise);
        CHECK(memcmp(test_zip_find(&listing, "dir/noise.bin")->data, noise, sizeof noise) == 0);
        CHECK_INT_EQ(test_zip_find(&listing, "dir/zeros.bin")->size, sizeof zeros);
        CHECK(memcmp(test_zip_find(&listing, "dir/zeros.bin")->data, zeros, sizeof zeros) == 0);
        test_zip_close(&listing);
    }
    body_free(&zip);
}

static void entries_keep_the_order_they_were_added_in(void)
{
    struct tm when = entry_time();
    body_t zip = { NULL, 0 };
    test_zip_t listing;
    zip_writer_t *writer = zip_writer_open(&when, 0, 16);
    CHECK(zip_writer_add_file(writer, "z.sav", "1", 1));
    CHECK(zip_writer_add_directory(writer, "m"));
    CHECK(zip_writer_add_file(writer, "a.sav", "2", 1));
    finish_into(writer, &zip);
    bool is_listed = test_zip_open(zip.data, (size_t)zip.size, &listing);
    CHECK(is_listed);
    if (is_listed) {
        CHECK_STR_EQ(listing.items[0].name, "z.sav");
        CHECK_STR_EQ(listing.items[1].name, "m/");
        CHECK_STR_EQ(listing.items[2].name, "a.sav");
        test_zip_close(&listing);
    }
    body_free(&zip);
}

static void every_entry_carries_the_given_timestamp(void)
{
    struct tm when = entry_time();
    struct tm expected_tm = when;
    body_t zip = { NULL, 0 };
    test_zip_t listing;
    expected_tm.tm_isdst = -1;
    time_t expected = mktime(&expected_tm);
    zip_writer_t *writer = zip_writer_open(&when, 0, 16);
    CHECK(zip_writer_add_directory(writer, "d"));
    CHECK(zip_writer_add_file(writer, "d/a.sav", "x", 1));
    CHECK(zip_writer_add_file(writer, "b.sav", "y", 1));
    finish_into(writer, &zip);
    bool is_listed = test_zip_open(zip.data, (size_t)zip.size, &listing);
    CHECK(is_listed);
    if (is_listed) {
        CHECK_INT_EQ(listing.count, 3);
        for (int i = 0; i < listing.count; i++) {
            CHECK_INT_EQ((long long)listing.items[i].modified, (long long)expected);
        }
        test_zip_close(&listing);
    }
    body_free(&zip);
}

static void the_same_input_gives_the_same_bytes(void)
{
    struct tm when = entry_time();
    body_t first = { NULL, 0 };
    body_t second = { NULL, 0 };
    for (int round = 0; round < 2; round++) {
        zip_writer_t *writer = zip_writer_open(&when, 0, 32);
        CHECK(zip_writer_add_directory(writer, "d"));
        CHECK(zip_writer_add_file(writer, "d/a.sav", "hello hello hello hello", 23));
        finish_into(writer, round == 0 ? &first : &second);
    }
    CHECK_INT_EQ(first.size, second.size);
    CHECK(first.size > 0 && memcmp(first.data, second.data, (size_t)first.size) == 0);
    body_free(&first);
    body_free(&second);
}

static void a_different_timestamp_changes_the_bytes_but_not_the_content(void)
{
    struct tm early = entry_time();
    struct tm late = entry_time();
    body_t first = { NULL, 0 };
    body_t second = { NULL, 0 };
    test_zip_t first_listing;
    test_zip_t second_listing;
    late.tm_hour = 18;
    zip_writer_t *writer = zip_writer_open(&early, 0, 8);
    CHECK(zip_writer_add_file(writer, "a.sav", "abc", 3));
    finish_into(writer, &first);
    writer = zip_writer_open(&late, 0, 8);
    CHECK(zip_writer_add_file(writer, "a.sav", "abc", 3));
    finish_into(writer, &second);
    bool are_listed = test_zip_open(first.data, (size_t)first.size, &first_listing);
    are_listed = test_zip_open(second.data, (size_t)second.size, &second_listing) && are_listed;
    CHECK(are_listed);
    if (are_listed) {
        CHECK(first_listing.items[0].modified != second_listing.items[0].modified);
        CHECK_INT_EQ(first_listing.items[0].crc, second_listing.items[0].crc);
    }
    test_zip_close(&first_listing);
    test_zip_close(&second_listing);
    body_free(&first);
    body_free(&second);
}

static void an_archive_without_entries_is_still_a_readable_zip(void)
{
    struct tm when = entry_time();
    body_t zip = { NULL, 0 };
    test_zip_t listing;
    zip_writer_t *writer = zip_writer_open(&when, 0, 0);
    finish_into(writer, &zip);
    CHECK(zip.size > 0);
    bool is_listed = test_zip_open(zip.data, (size_t)zip.size, &listing);
    CHECK(is_listed);
    if (is_listed) {
        CHECK_INT_EQ(listing.count, 0);
        test_zip_close(&listing);
    }
    body_free(&zip);
}

static void the_finished_body_is_nul_terminated_after_the_zip_bytes(void)
{
    struct tm when = entry_time();
    body_t zip = { NULL, 0 };
    zip_writer_t *writer = zip_writer_open(&when, 0, 8);
    CHECK(zip_writer_add_file(writer, "a.sav", "abc", 3));
    finish_into(writer, &zip);
    CHECK(zip.size > 4);
    CHECK_INT_EQ(zip.data[0], 'P');
    CHECK_INT_EQ(zip.data[1], 'K');
    CHECK_INT_EQ(zip.data[zip.size], 0);
    body_free(&zip);
}

static void a_directory_name_too_long_for_the_buffer_is_refused(void)
{
    struct tm when = entry_time();
    char name[400];
    zip_writer_t *writer = zip_writer_open(&when, 0, 8);
    memset(name, 'd', sizeof name - 1);
    name[sizeof name - 1] = '\0';
    CHECK(!zip_writer_add_directory(writer, name));
    zip_writer_abort(writer);
}

static void fill_with_noise(unsigned char *bytes, size_t size, unsigned seed)
{
    for (size_t i = 0; i < size; i++) {
        seed = seed * 1664525u + 1013904223u;
        bytes[i] = (unsigned char)(seed >> 24);
    }
}

static void incompressible_files_fit_the_preallocated_buffer_without_growing(void)
{
    enum { FILE_COUNT = 5, FILE_BYTES = 150000 };
    struct tm when = entry_time();
    body_t zip = { NULL, 0 };
    unsigned char *noise = malloc(FILE_BYTES);
    zip_writer_t *writer = zip_writer_open(&when, FILE_COUNT + 1, (size_t)FILE_COUNT * FILE_BYTES);
    CHECK(noise != NULL && writer != NULL);
    CHECK(zip_writer_add_directory(writer, "slot_00"));
    for (int i = 0; i < FILE_COUNT; i++) {
        char name[32];
        snprintf(name, sizeof name, "slot_00/file_%d.bin", i);
        fill_with_noise(noise, FILE_BYTES, (unsigned)i + 1);
        CHECK(zip_writer_add_file(writer, name, noise, FILE_BYTES));
    }
    CHECK_INT_EQ(zip_writer_heap_growths(writer), 0);
    finish_into(writer, &zip);
    CHECK(zip.size > (long long)FILE_COUNT * FILE_BYTES);
    body_free(&zip);
    free(noise);
}

void run_zip_writer_tests(void)
{
    RUN_TEST(a_directory_name_gets_a_trailing_slash_exactly_once);
    RUN_TEST(files_come_back_byte_for_byte_including_empty_and_incompressible);
    RUN_TEST(entries_keep_the_order_they_were_added_in);
    RUN_TEST(every_entry_carries_the_given_timestamp);
    RUN_TEST(the_same_input_gives_the_same_bytes);
    RUN_TEST(a_different_timestamp_changes_the_bytes_but_not_the_content);
    RUN_TEST(an_archive_without_entries_is_still_a_readable_zip);
    RUN_TEST(the_finished_body_is_nul_terminated_after_the_zip_bytes);
    RUN_TEST(a_directory_name_too_long_for_the_buffer_is_refused);
    RUN_TEST(incompressible_files_fit_the_preallocated_buffer_without_growing);
}
