#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "save_fingerprint.h"
#include "sha256.h"
#include "test.h"
#include "test_fs.h"
#include "test_support.h"

static void read_contract_fingerprint(char out[SAVE_FINGERPRINT_SIZE])
{
    char *text = test_read_file(CONTRACTS_DIR "/save-tree.fingerprint.txt");
    CHECK(text != NULL);
    size_t length = strcspn(text, "\r\n");
    snprintf(out, SAVE_FINGERPRINT_SIZE, "%.*s", (int)length, text);
    free(text);
}

static void the_shared_save_tree_has_the_same_fingerprint_as_on_the_pc(void)
{
    /* contracts/save-tree holds junk (desktop.ini, Thumbs.db, x.zip) and nested folders; the PC launcher and
       contracts/fingerprint.py give the fingerprint in save-tree.fingerprint.txt. */
    char expected[SAVE_FINGERPRINT_SIZE];
    char actual[SAVE_FINGERPRINT_SIZE];
    char problem[TS_PATH_SIZE];
    read_contract_fingerprint(expected);
    CHECK_INT_EQ(save_fingerprint_dir(CONTRACTS_DIR "/save-tree", actual, problem, sizeof problem), TREE_OK);
    CHECK_STR_EQ(actual, expected);
}

static void the_contract_save_with_an_empty_album_and_a_jksv_meta_file_keeps_the_fingerprint(void)
{
    char root[256];
    char expected[SAVE_FINGERPRINT_SIZE];
    char actual[SAVE_FINGERPRINT_SIZE];
    char problem[TS_PATH_SIZE];
    CHECK(test_fs_make_contract_save(root, sizeof root));
    CHECK(test_fs_write_file(root, ".nx_save_meta.bin", "JKSV", 4));
    CHECK(test_fs_make_dir(root, "slot_09/empty"));
    read_contract_fingerprint(expected);
    CHECK_INT_EQ(save_fingerprint_dir(root, actual, problem, sizeof problem), TREE_OK);
    CHECK_STR_EQ(actual, expected);
}

static void the_fingerprint_follows_the_published_formula_for_files_larger_than_a_read_chunk(void)
{
    size_t big_size = 200 * 1024 + 17;
    unsigned char *big = malloc(big_size);
    for (size_t i = 0; i < big_size; i++) {
        big[i] = (unsigned char)(i * 31 + i / 251);
    }
    char root[256];
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "b/big.sav", big, big_size));
    CHECK(test_fs_write_file(root, "a.sav", "A", 1));
    CHECK(test_fs_write_file(root, "B.sav", "", 0));

    char big_hash[SHA256_HEX_SIZE];
    char a_hash[SHA256_HEX_SIZE];
    char empty_hash[SHA256_HEX_SIZE];
    sha256_hex(big, big_size, big_hash);
    sha256_hex("A", 1, a_hash);
    sha256_hex("", 0, empty_hash);
    char lines[512];
    /* Byte order: "B.sav" < "a.sav" < "b/big.sav". */
    snprintf(lines, sizeof lines, "B.sav\n%s\na.sav\n%s\nb/big.sav\n%s\n", empty_hash, a_hash, big_hash);
    char outer[SHA256_HEX_SIZE];
    char expected[SAVE_FINGERPRINT_SIZE];
    sha256_hex(lines, strlen(lines), outer);
    snprintf(expected, sizeof expected, "sha256:%s", outer);

    char actual[SAVE_FINGERPRINT_SIZE];
    char problem[TS_PATH_SIZE];
    CHECK_INT_EQ(save_fingerprint_dir(root, actual, problem, sizeof problem), TREE_OK);
    CHECK_STR_EQ(actual, expected);
    free(big);
}

static void a_change_of_one_byte_changes_the_fingerprint(void)
{
    char root[256];
    char before[SAVE_FINGERPRINT_SIZE];
    char after[SAVE_FINGERPRINT_SIZE];
    char problem[TS_PATH_SIZE];
    CHECK(test_fs_make_contract_save(root, sizeof root));
    CHECK_INT_EQ(save_fingerprint_dir(root, before, problem, sizeof problem), TREE_OK);
    CHECK(test_fs_write_file(root, "slot_00/progress.sav", "progress, one byte more!", 24));
    CHECK_INT_EQ(save_fingerprint_dir(root, after, problem, sizeof problem), TREE_OK);
    CHECK(strcmp(before, after) != 0);
}

static void a_name_the_pc_would_reject_is_reported_and_gives_no_fingerprint(void)
{
    char root[256];
    char actual[SAVE_FINGERPRINT_SIZE];
    char problem[TS_PATH_SIZE];
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "slot_00/\xE9.sav", "x", 1));
    CHECK_INT_EQ(save_fingerprint_dir(root, actual, problem, sizeof problem), TREE_ERR_BAD_NAME);
    CHECK_STR_EQ(actual, "");
    CHECK(strncmp(problem, "slot_00/", 8) == 0); /* the name comes back in the host's code page */
    CHECK(strlen(problem) > 8);
}

static void a_missing_folder_is_a_read_error(void)
{
    char actual[SAVE_FINGERPRINT_SIZE];
    char problem[TS_PATH_SIZE];
    CHECK_INT_EQ(save_fingerprint_dir("build/tmp/no-such-save", actual, problem, sizeof problem), TREE_ERR_READ);
    CHECK_STR_EQ(actual, "");
}

void run_save_fingerprint_tests(void)
{
    RUN_TEST(the_shared_save_tree_has_the_same_fingerprint_as_on_the_pc);
    RUN_TEST(the_contract_save_with_an_empty_album_and_a_jksv_meta_file_keeps_the_fingerprint);
    RUN_TEST(the_fingerprint_follows_the_published_formula_for_files_larger_than_a_read_chunk);
    RUN_TEST(a_change_of_one_byte_changes_the_fingerprint);
    RUN_TEST(a_name_the_pc_would_reject_is_reported_and_gives_no_fingerprint);
    RUN_TEST(a_missing_folder_is_a_read_error);
}
