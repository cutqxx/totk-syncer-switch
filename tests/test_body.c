#include <stdio.h>
#include <string.h>

#include "body.h"
#include "test.h"
#include "test_fs.h"

static body_status_t load_from(const void *content, size_t size, int64_t max_size, body_t *body)
{
    char root[128];
    char path[256];
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "f.bin", content, size));
    snprintf(path, sizeof path, "%s/f.bin", root);
    return body_load(path, max_size, body);
}

static void loads_file_content_and_size(void)
{
    body_t body;
    CHECK_INT_EQ(load_from("PK123", 5, 100, &body), BODY_OK);
    CHECK_INT_EQ(body.size, 5);
    CHECK(memcmp(body.data, "PK123", 5) == 0);
    body_free(&body);
}

static void accepts_file_exactly_at_max_size(void)
{
    body_t body;
    CHECK_INT_EQ(load_from("12345", 5, 5, &body), BODY_OK);
    body_free(&body);
}

static void reports_not_found_for_missing_file(void)
{
    body_t body;
    CHECK_INT_EQ(body_load("build/tmp/definitely/missing.zip", 100, &body), BODY_ERR_NOT_FOUND);
    CHECK(body.data == NULL);
}

static void reports_empty_for_zero_byte_file(void)
{
    body_t body;
    CHECK_INT_EQ(load_from("", 0, 100, &body), BODY_ERR_EMPTY);
    CHECK(body.data == NULL);
}

static void reports_too_large_with_real_size_and_no_data(void)
{
    body_t body;
    CHECK_INT_EQ(load_from("123456", 6, 5, &body), BODY_ERR_TOO_LARGE);
    CHECK_INT_EQ(body.size, 6);
    CHECK(body.data == NULL);
}

static void free_resets_body(void)
{
    body_t body;
    CHECK_INT_EQ(load_from("abc", 3, 100, &body), BODY_OK);
    body_free(&body);
    CHECK(body.data == NULL);
    CHECK_INT_EQ(body.size, 0);
}

static void message_for_missing_file_names_the_path(void)
{
    char msg[256];
    body_status_message(BODY_ERR_NOT_FOUND, "sd:/a/test.zip", 0, 100, msg, sizeof msg);
    CHECK_STR_EQ(msg, "File not found: sd:/a/test.zip");
}

static void message_for_empty_file_mentions_zero_bytes(void)
{
    char msg[256];
    body_status_message(BODY_ERR_EMPTY, "sd:/a/test.zip", 0, 100, msg, sizeof msg);
    CHECK_STR_EQ(msg, "File is empty (0 bytes): sd:/a/test.zip");
}

static void message_for_too_large_file_shows_actual_and_maximum_size(void)
{
    char msg[256];
    body_status_message(BODY_ERR_TOO_LARGE, "sd:/a/test.zip", 70000000, 67108864, msg, sizeof msg);
    CHECK_STR_EQ(msg, "File too large (70000000 bytes, maximum 67108864 bytes): sd:/a/test.zip");
}

static void message_for_no_memory_names_the_path(void)
{
    char msg[256];
    body_status_message(BODY_ERR_NO_MEMORY, "sd:/a/test.zip", 0, 100, msg, sizeof msg);
    CHECK_STR_EQ(msg, "Not enough memory to load file: sd:/a/test.zip");
}

static void message_for_read_error_says_unreadable(void)
{
    char msg[256];
    body_status_message(BODY_ERR_READ, "sd:/a/test.zip", 0, 100, msg, sizeof msg);
    CHECK_STR_EQ(msg, "Cannot read sd:/a/test.zip");
}

void run_body_tests(void)
{
    RUN_TEST(loads_file_content_and_size);
    RUN_TEST(accepts_file_exactly_at_max_size);
    RUN_TEST(reports_not_found_for_missing_file);
    RUN_TEST(reports_empty_for_zero_byte_file);
    RUN_TEST(reports_too_large_with_real_size_and_no_data);
    RUN_TEST(free_resets_body);
    RUN_TEST(message_for_missing_file_names_the_path);
    RUN_TEST(message_for_empty_file_mentions_zero_bytes);
    RUN_TEST(message_for_too_large_file_shows_actual_and_maximum_size);
    RUN_TEST(message_for_no_memory_names_the_path);
    RUN_TEST(message_for_read_error_says_unreadable);
}
