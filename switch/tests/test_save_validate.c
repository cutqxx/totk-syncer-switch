#include <stdlib.h>
#include <string.h>

#include "body.h"
#include "save_validate.h"
#include "test.h"
#include "test_support.h"
#include "test_zip_read.h"
#include "zip_writer.h"

typedef struct {
    bool is_valid;
    char reason[SAVE_VALIDATE_REASON_SIZE];
    save_zip_stats_t stats;
} verdict_t;

static verdict_t validate_bytes(const void *bytes, size_t size)
{
    verdict_t verdict;
    memset(&verdict, 0, sizeof verdict);
    zip_reader_t *zip = zip_reader_open(bytes, size);
    verdict.is_valid = save_validate_zip(zip, &verdict.stats, verdict.reason, sizeof verdict.reason);
    zip_reader_close(zip);
    return verdict;
}

static bool validate_contract_file(const char *file, verdict_t *out)
{
    char path[512];
    snprintf(path, sizeof path, "%s/save-zip/%s", CONTRACTS_DIR, file);
    FILE *handle = fopen(path, "rb");
    if (handle == NULL) {
        return false;
    }
    fseek(handle, 0, SEEK_END);
    long size = ftell(handle);
    fseek(handle, 0, SEEK_SET);
    unsigned char *bytes = malloc((size_t)size + 1);
    bool is_read = bytes != NULL && fread(bytes, 1, (size_t)size, handle) == (size_t)size;
    fclose(handle);
    if (is_read) {
        *out = validate_bytes(bytes, (size_t)size);
    }
    free(bytes);
    return is_read;
}

static int array_size_or_zero(const cJSON *item, const char *name)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(item, name);
    return cJSON_IsArray(array) ? cJSON_GetArraySize(array) : 0;
}

static void every_contract_zip_gets_the_launchers_verdict_and_reason(void)
{
    cJSON *cases = test_load_cases();
    CHECK(cases != NULL);
    int checked = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, cases)
    {
        const char *file = cJSON_GetObjectItemCaseSensitive(item, "file")->valuestring;
        bool is_valid_expected = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "valid"));
        verdict_t verdict;
        bool is_read = validate_contract_file(file, &verdict);
        CHECK(is_read);
        if (!is_read) {
            continue;
        }
        checked++;
        if (verdict.is_valid != is_valid_expected) {
            printf("  %s: valid=%d expected %d (%s)\n", file, verdict.is_valid, is_valid_expected, verdict.reason);
        }
        CHECK(verdict.is_valid == is_valid_expected);
        if (is_valid_expected) {
            CHECK_INT_EQ(verdict.stats.files, array_size_or_zero(item, "saved_files"));
            CHECK_INT_EQ(verdict.stats.directories, array_size_or_zero(item, "saved_dirs"));
        } else {
            CHECK_STR_EQ(verdict.reason, cJSON_GetObjectItemCaseSensitive(item, "reason")->valuestring);
        }
    }
    CHECK(checked >= 30);
    cJSON_Delete(cases);
}

static void garbage_and_empty_input_are_not_a_zip(void)
{
    verdict_t garbage = validate_bytes("not a zip at all", 16);
    CHECK(!garbage.is_valid);
    CHECK_STR_EQ(garbage.reason, "not a zip file");
    verdict_t empty = validate_bytes(NULL, 0);
    CHECK(!empty.is_valid);
    CHECK_STR_EQ(empty.reason, "not a zip file");
}

static void a_reason_with_a_long_or_non_ascii_name_is_cut_and_ascii(void)
{
    struct tm when = { .tm_year = 126, .tm_mon = 10, .tm_mday = 3 };
    char long_name[200];
    memset(long_name, 'x', sizeof long_name - 1);
    long_name[0] = '\x80';
    long_name[sizeof long_name - 1] = '\0';
    zip_writer_t *writer = zip_writer_open(&when, 2, 8);
    CHECK(zip_writer_add_file(writer, "option.sav", "o", 1));
    CHECK(zip_writer_add_file(writer, long_name, "z", 1));
    body_t zip = { NULL, 0 };
    CHECK(zip_writer_finish(writer, &zip));

    verdict_t verdict = validate_bytes(zip.data, (size_t)zip.size);

    CHECK(!verdict.is_valid);
    CHECK_INT_EQ(strlen(verdict.reason), strlen("unsafe path: ") + 80);
    CHECK(strncmp(verdict.reason, "unsafe path: ?xxx", 17) == 0);
    CHECK(test_is_ascii_text(verdict.reason));
    body_free(&zip);
}

void run_save_validate_tests(void)
{
    RUN_TEST(every_contract_zip_gets_the_launchers_verdict_and_reason);
    RUN_TEST(garbage_and_empty_input_are_not_a_zip);
    RUN_TEST(a_reason_with_a_long_or_non_ascii_name_is_cut_and_ascii);
}
