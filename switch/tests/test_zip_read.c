#include "test_zip_read.h"

#include "save_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"
#include "test_sha256.h"
#include "test_support.h"

static bool read_item(mz_zip_archive *archive, mz_uint index, test_zip_item_t *item)
{
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(archive, index, &stat) || strlen(stat.m_filename) >= TEST_ZIP_NAME_SIZE) {
        return false;
    }
    memcpy(item->name, stat.m_filename, strlen(stat.m_filename) + 1);
    item->is_directory = mz_zip_reader_is_file_a_directory(archive, index) != MZ_FALSE;
    item->crc = stat.m_crc32;
    item->size = stat.m_uncomp_size;
    item->modified = stat.m_time;
    if (item->is_directory || item->size == 0) {
        return true;
    }
    size_t extracted_size = 0;
    item->data = mz_zip_reader_extract_to_heap(archive, index, &extracted_size, 0);
    return item->data != NULL && extracted_size == item->size;
}

bool test_zip_open(const unsigned char *bytes, size_t size, test_zip_t *out)
{
    mz_zip_archive archive;
    memset(&archive, 0, sizeof archive);
    memset(out, 0, sizeof *out);
    if (!mz_zip_reader_init_mem(&archive, bytes, size, 0)) {
        return false;
    }
    int count = (int)mz_zip_reader_get_num_files(&archive);
    out->items = calloc((size_t)count + 1, sizeof *out->items);
    bool is_read = out->items != NULL;
    for (int i = 0; i < count && is_read; i++) {
        is_read = read_item(&archive, (mz_uint)i, &out->items[i]);
        out->count = i + 1;
    }
    mz_zip_reader_end(&archive);
    if (!is_read) {
        test_zip_close(out);
    }
    return is_read;
}

void test_zip_close(test_zip_t *zip)
{
    for (int i = 0; i < zip->count; i++) {
        free(zip->items[i].data);
    }
    free(zip->items);
    memset(zip, 0, sizeof *zip);
}

const test_zip_item_t *test_zip_find(const test_zip_t *zip, const char *name)
{
    for (int i = 0; i < zip->count; i++) {
        if (strcmp(zip->items[i].name, name) == 0) {
            return &zip->items[i];
        }
    }
    return NULL;
}

static int compare_item_pointers(const void *left, const void *right)
{
    return strcmp((*(const test_zip_item_t *const *)left)->name, (*(const test_zip_item_t *const *)right)->name);
}

void test_zip_fingerprint(const test_zip_t *zip, char out[80])
{
    const test_zip_item_t **files = calloc((size_t)zip->count + 1, sizeof *files);
    int file_count = 0;
    size_t text_size = 1;
    for (int i = 0; i < zip->count; i++) {
        if (!zip->items[i].is_directory) {
            files[file_count++] = &zip->items[i];
            text_size += strlen(zip->items[i].name) + 1 + 64 + 1;
        }
    }
    qsort(files, (size_t)file_count, sizeof *files, compare_item_pointers);
    char *text = calloc(text_size, 1);
    for (int i = 0; i < file_count; i++) {
        char hash[65];
        test_sha256_hex(files[i]->data, (size_t)files[i]->size, hash);
        sprintf(text + strlen(text), "%s\n%s\n", files[i]->name, hash);
    }
    char outer[65];
    test_sha256_hex(text, strlen(text), outer);
    snprintf(out, 80, "sha256:%s", outer);
    free(text);
    free(files);
}

cJSON *test_load_cases(void)
{
    char *text = test_read_file(CONTRACTS_DIR "/save-zip/cases.json");
    cJSON *cases = text == NULL ? NULL : cJSON_Parse(text);
    free(text);
    return cases;
}

const cJSON *test_find_case(const cJSON *cases, const char *zip_file)
{
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, cases)
    {
        const cJSON *file = cJSON_GetObjectItemCaseSensitive(item, "file");
        if (cJSON_IsString(file) && strcmp(file->valuestring, zip_file) == 0) {
            return item;
        }
    }
    return NULL;
}

void test_tree_identity(const char *root, char *out, size_t out_size)
{
    static const struct tm when = { .tm_year = 126, .tm_mon = 10, .tm_mday = 3 };
    body_t zip = { NULL, 0 };
    pack_stats_t stats;
    test_zip_t listing;
    char fingerprint[80] = "";
    out[0] = '\0';
    if (save_pack_dir(root, &when, 64LL * 1024 * 1024, &zip, &stats) != PACK_OK
        || !test_zip_open(zip.data, (size_t)zip.size, &listing)) {
        snprintf(out, out_size, "unreadable");
        body_free(&zip);
        return;
    }
    test_zip_fingerprint(&listing, fingerprint);
    snprintf(out, out_size, "%s", fingerprint);
    for (int i = 0; i < listing.count; i++) {
        size_t used = strlen(out);
        snprintf(out + used, out_size - used, "|%s", listing.items[i].name);
    }
    test_zip_close(&listing);
    body_free(&zip);
}
