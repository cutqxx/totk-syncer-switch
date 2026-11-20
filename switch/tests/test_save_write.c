#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "save_pack.h"
#include "save_write.h"
#include "test.h"
#include "test_fs.h"
#include "test_support.h"
#include "test_zip_read.h"
#include "zip_writer.h"

#define BIG_LIMIT (64LL * 1024 * 1024)
#define BIG_JOURNAL (100LL * 1024 * 1024)

static const struct tm ZIP_TIME = { .tm_year = 126, .tm_mon = 10, .tm_mday = 3 };

static body_t read_contract_zip(const char *name)
{
    char path[512];
    body_t zip = { NULL, 0 };
    snprintf(path, sizeof path, "%s/save-zip/%s", CONTRACTS_DIR, name);
    char *bytes = NULL;
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        bytes = malloc((size_t)size);
        if (bytes != NULL && fread(bytes, 1, (size_t)size, file) == (size_t)size) {
            body_append(&zip, bytes, (size_t)size, BIG_LIMIT);
        }
        free(bytes);
        fclose(file);
    }
    CHECK(zip.size > 0);
    return zip;
}

/* A save with one big file in each slot, so a small journal forces commits in the middle of a file. */
static body_t big_zip(size_t file_size)
{
    unsigned char *content = malloc(file_size);
    for (size_t i = 0; i < file_size; i++) {
        content[i] = (unsigned char)((i * 2654435761u) >> 13);
    }
    zip_writer_t *writer = zip_writer_open(&ZIP_TIME, 6, 4 * file_size);
    CHECK(zip_writer_add_directory(writer, "slot_00"));
    CHECK(zip_writer_add_directory(writer, "album"));
    CHECK(zip_writer_add_file(writer, "option.sav", content, file_size));
    CHECK(zip_writer_add_file(writer, "slot_00/progress.sav", content, file_size));
    CHECK(zip_writer_add_file(writer, "slot_00/caption.sav", content, file_size / 3));
    CHECK(zip_writer_add_file(writer, "Thumbs.db", "junk", 4));
    body_t zip = { NULL, 0 };
    CHECK(zip_writer_finish(writer, &zip));
    free(content);
    return zip;
}

static bool exists(const char *root, const char *rel)
{
    char path[512];
    struct stat info;
    snprintf(path, sizeof path, "%s/%s", root, rel);
    return stat(path, &info) == 0;
}

typedef struct {
    fake_world_t world;
    fake_console_t console;
    faulty_files_t files;
    char save[256];
    save_write_result_t result;
} write_fixture_t;

/* A mounted save that already holds a different save and some leftovers. */
static void write_fixture_open(write_fixture_t *f)
{
    memset(f, 0, sizeof *f);
    fake_world_init(&f->world, NULL, 0);
    fake_console_init(&f->console, &f->world);
    faulty_files_init(&f->files);
    f->console.watched_files = &f->files;
    CHECK(test_fs_make_root(f->save, sizeof f->save));
    CHECK(test_fs_write_file(f->save, "option.sav", "old option", 10));
    CHECK(test_fs_write_file(f->save, "slot_00/progress.sav", "old progress", 12));
    CHECK(test_fs_write_file(f->save, "slot_05/only-on-switch.sav", "stale", 5));
    CHECK(test_fs_make_dir(f->save, "old/empty/dir"));
}

static void write_zip(write_fixture_t *f, const body_t *zip, int64_t journal_bytes)
{
    zip_reader_t *reader = zip_reader_open(zip->data, (size_t)zip->size);
    CHECK(reader != NULL);
    save_write_zip(&f->files.ops, &f->console.port, f->save, reader, journal_bytes, &f->result);
    zip_reader_close(reader);
}

static void the_save_becomes_exactly_the_zip_with_empty_dirs_and_without_junk(void)
{
    write_fixture_t f;
    write_fixture_open(&f);
    body_t zip = read_contract_zip("valid-save-tree.zip");
    char expected[4096];
    char actual[4096];
    char contract_save[256];
    CHECK(test_fs_make_contract_save(contract_save, sizeof contract_save));
    test_tree_identity(contract_save, expected, sizeof expected);

    write_zip(&f, &zip, BIG_JOURNAL);

    CHECK(f.result.is_ok);
    test_tree_identity(f.save, actual, sizeof actual);
    CHECK_STR_EQ(actual, expected);
    CHECK(exists(f.save, "album"));
    CHECK(!exists(f.save, "slot_05/only-on-switch.sav"));
    CHECK(!exists(f.save, "old"));
    CHECK(!exists(f.save, "Thumbs.db"));
    CHECK(!exists(f.save, "desktop.ini"));
    CHECK(!exists(f.save, ".nx_save_meta.bin"));
    CHECK(!exists(f.save, "x.zip"));
    body_free(&zip);
}

static void the_jksv_meta_file_is_never_written_into_the_save(void)
{
    write_fixture_t f;
    write_fixture_open(&f);
    body_t zip = read_contract_zip("valid-jksv-meta.zip");

    write_zip(&f, &zip, BIG_JOURNAL);

    CHECK(f.result.is_ok);
    CHECK(!exists(f.save, ".nx_save_meta.bin"));
    CHECK(exists(f.save, "option.sav"));
    body_free(&zip);
}

static void a_save_that_fits_the_journal_is_committed_once_at_the_end(void)
{
    write_fixture_t f;
    write_fixture_open(&f);
    body_t zip = big_zip(100 * 1024);

    write_zip(&f, &zip, BIG_JOURNAL);

    CHECK(f.result.is_ok);
    CHECK_INT_EQ(f.console.commit_calls, 1);
    CHECK_INT_EQ(f.result.commits, 1);
    body_free(&zip);
}

static void a_small_journal_gets_commits_in_time_and_never_with_an_open_file(void)
{
    write_fixture_t f;
    write_fixture_open(&f);
    body_t zip = big_zip(300 * 1024);
    int64_t journal = 128 * 1024;

    write_zip(&f, &zip, journal);

    CHECK(f.result.is_ok);
    CHECK(f.files.max_bytes_between_commits <= save_write_budget(journal));
    CHECK(f.console.commit_calls >= (int)((3 * 300 * 1024 - 1) / save_write_budget(journal)));
    CHECK_INT_EQ(f.console.commits_with_open_files, 0);
    char expected[4096];
    char actual[4096];
    write_fixture_t reference;
    write_fixture_open(&reference);
    write_zip(&reference, &zip, BIG_JOURNAL);
    test_tree_identity(reference.save, expected, sizeof expected);
    test_tree_identity(f.save, actual, sizeof actual);
    CHECK_STR_EQ(actual, expected);
    body_free(&zip);
}

static void a_save_larger_than_the_budget_is_also_committed_after_every_file(void)
{
    write_fixture_t f;
    write_fixture_open(&f);
    body_t zip = big_zip(40 * 1024);
    int64_t journal = 64 * 1024;

    write_zip(&f, &zip, journal);

    CHECK(f.result.is_ok);
    CHECK(f.console.commit_calls >= 3 + 1);
    CHECK_INT_EQ(f.console.commits_with_open_files, 0);
    body_free(&zip);
}

static void an_unknown_journal_size_uses_the_small_fallback_budget(void)
{
    CHECK_INT_EQ(save_write_budget(0), SAVE_WRITE_FALLBACK_BUDGET);
    CHECK_INT_EQ(save_write_budget(-5), SAVE_WRITE_FALLBACK_BUDGET);
    CHECK_INT_EQ(save_write_budget(4 * 1024 * 1024), 3 * 1024 * 1024);
}

/* Every single failing step: the write stops, says why, and reports a change exactly when something changed. */
static void a_failure_at_any_step_is_reported_and_tells_whether_the_save_changed(void)
{
    static const fault_step_t steps[] = { FAULT_REMOVE_FILE, FAULT_REMOVE_DIR, FAULT_MAKE_DIR, FAULT_OPEN, FAULT_WRITE, FAULT_CLOSE };
    body_t zip = big_zip(40 * 1024);
    int cases = 0;
    for (size_t s = 0; s < sizeof steps / sizeof steps[0]; s++) {
        for (int at = 1; at <= 12; at++) {
            write_fixture_t f;
            write_fixture_open(&f);
            f.files.fail_step = steps[s];
            f.files.fail_at = at;

            write_zip(&f, &zip, 64 * 1024);

            if (f.files.calls[steps[s]] < at) {
                CHECK(f.result.is_ok);
                continue;
            }
            cases++;
            CHECK(!f.result.is_ok);
            CHECK(f.result.reason[0] != '\0');
            CHECK(strstr(f.result.reason, "No space left on device") != NULL);
            CHECK(test_is_ascii_text(f.result.reason));
            CHECK(f.result.is_changed == (f.files.mutations > 0 || f.result.commits > 0));
        }
    }
    CHECK(cases >= 20);
    body_free(&zip);
}

static void the_first_failing_remove_leaves_the_save_reported_unchanged(void)
{
    write_fixture_t f;
    write_fixture_open(&f);
    CHECK(test_fs_make_root(f.save, sizeof f.save));
    CHECK(test_fs_write_file(f.save, "option.sav", "only file", 9));
    char before[4096];
    test_tree_identity(f.save, before, sizeof before);
    f.files.fail_step = FAULT_REMOVE_FILE;
    f.files.fail_at = 1;
    body_t zip = big_zip(1024);

    write_zip(&f, &zip, BIG_JOURNAL);

    char after[4096];
    test_tree_identity(f.save, after, sizeof after);
    CHECK(!f.result.is_ok);
    CHECK(!f.result.is_changed);
    CHECK_STR_EQ(after, before);
    CHECK_INT_EQ(f.console.commit_calls, 0);
    body_free(&zip);
}

static void a_failing_commit_fails_the_write_and_counts_as_a_change(void)
{
    for (int at = 1; at <= 3; at++) {
        write_fixture_t f;
        write_fixture_open(&f);
        f.console.fail_commit_at = at;
        f.console.commit_error = 0x1234;
        body_t zip = big_zip(40 * 1024);

        write_zip(&f, &zip, 64 * 1024);

        CHECK(!f.result.is_ok);
        CHECK(f.result.is_changed);
        CHECK_STR_EQ(f.result.reason, "commit failed (error 0x1234)");
        CHECK_INT_EQ(f.console.commit_calls, at);
        body_free(&zip);
    }
}

void run_save_write_tests(void)
{
    RUN_TEST(the_save_becomes_exactly_the_zip_with_empty_dirs_and_without_junk);
    RUN_TEST(the_jksv_meta_file_is_never_written_into_the_save);
    RUN_TEST(a_save_that_fits_the_journal_is_committed_once_at_the_end);
    RUN_TEST(a_small_journal_gets_commits_in_time_and_never_with_an_open_file);
    RUN_TEST(a_save_larger_than_the_budget_is_also_committed_after_every_file);
    RUN_TEST(an_unknown_journal_size_uses_the_small_fallback_budget);
    RUN_TEST(a_failure_at_any_step_is_reported_and_tells_whether_the_save_changed);
    RUN_TEST(the_first_failing_remove_leaves_the_save_reported_unchanged);
    RUN_TEST(a_failing_commit_fails_the_write_and_counts_as_a_change);
}
