#ifndef TS_TEST_ZIP_READ_H
#define TS_TEST_ZIP_READ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "cJSON.h"

#define TEST_ZIP_NAME_SIZE 300

typedef struct {
    char name[TEST_ZIP_NAME_SIZE]; /* exactly as stored; directories end with '/' */
    bool is_directory;
    uint32_t crc;
    uint64_t size;
    time_t modified;
    unsigned char *data; /* files only; NULL for an empty file */
} test_zip_item_t;

typedef struct {
    test_zip_item_t *items;
    int count;
} test_zip_t;

/* Reads every entry with miniz, checking CRCs. False when the bytes are not a readable ZIP. */
bool test_zip_open(const unsigned char *bytes, size_t size, test_zip_t *out);
void test_zip_close(test_zip_t *zip);

/* The entry stored under exactly this name, or NULL. */
const test_zip_item_t *test_zip_find(const test_zip_t *zip, const char *name);

/* The save fingerprint of the files in the ZIP, "sha256:<hex>", per contracts/README.md (every file counts, so the
   ZIP must hold no junk). */
void test_zip_fingerprint(const test_zip_t *zip, char out[80]);

/* The save fingerprint and the entry list of a directory tree as the PC would see it (packed without junk):
   equal text means equal content, whatever the file times. */
void test_tree_identity(const char *root, char *out, size_t out_size);

/* contracts/save-zip/cases.json parsed; the caller releases it with cJSON_Delete. NULL when unreadable. */
cJSON *test_load_cases(void);

/* The case object for this archive file name, or NULL. */
const cJSON *test_find_case(const cJSON *cases, const char *zip_file);

#endif
