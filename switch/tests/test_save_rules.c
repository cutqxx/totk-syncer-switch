#include <stdio.h>
#include <string.h>

#include "save_rules.h"
#include "test.h"
#include "test_zip_read.h"

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

static void check_segments_rejected(const char *const *names, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (save_rules_is_safe_segment(names[i])) {
            printf("  accepted but must be rejected: \"%s\"\n", names[i]);
        }
        CHECK(!save_rules_is_safe_segment(names[i]));
    }
}

static void check_segments_accepted(const char *const *names, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!save_rules_is_safe_segment(names[i])) {
            printf("  rejected but must be accepted: \"%s\"\n", names[i]);
        }
        CHECK(save_rules_is_safe_segment(names[i]));
    }
}

static void empty_and_dot_segments_are_unsafe(void)
{
    static const char *const names[] = { "", ".", ".." };
    check_segments_rejected(names, COUNT_OF(names));
}

static void every_character_windows_forbids_makes_a_segment_unsafe(void)
{
    static const char *const names[] = { "a\\b", "a:b", "a<b", "a>b", "a\"b", "a|b", "a?b", "a*b", "option.sav:stream", "C:" };
    check_segments_rejected(names, COUNT_OF(names));
}

static void control_characters_and_delete_make_a_segment_unsafe(void)
{
    static const char *const names[] = { "a\x01" "b", "a\tb", "a\nb", "a\rb", "a\x1f", "\x1f" "a", "a\x7f" "b", "a\x7f" };
    check_segments_rejected(names, COUNT_OF(names));
}

static void a_trailing_dot_or_space_makes_a_segment_unsafe(void)
{
    static const char *const names[] = { "caption.sav.", "caption.sav ", "x. ", "x .", "x..", "x  ", " " };
    check_segments_rejected(names, COUNT_OF(names));
}

static void windows_device_names_are_unsafe_in_any_letter_case(void)
{
    static const char *const names[] = {
        "CON", "con", "Con", "cOn", "PRN", "prn", "AUX", "aux", "NUL", "nul", "Nul", "CONIN$", "conin$", "CONOUT$", "ConOut$",
        "COM1", "com5", "COM9", "LPT1", "lpt5", "LPT9", "Lpt3",
    };
    check_segments_rejected(names, COUNT_OF(names));
}

static void a_device_name_with_an_extension_is_still_a_device(void)
{
    static const char *const names[] = { "CON.txt", "nul.dat", "con.tar.gz", "COM1.sav", "LPT9.x", "CONIN$.txt", "prn.a.b" };
    check_segments_rejected(names, COUNT_OF(names));
}

static void a_device_name_with_spaces_before_the_extension_is_still_a_device(void)
{
    static const char *const names[] = { "CON .txt", "nul  .dat", "aux   .x", "COM1 .sav", "lpt2  .a", "CONIN$ .txt" };
    check_segments_rejected(names, COUNT_OF(names));
}

static void names_that_only_look_like_devices_are_safe(void)
{
    static const char *const names[] = {
        "CONSOLE", "COM0", "COM10", "LPT0", "NULL", "AUXILIARY", "CON1", "ICON", "comx", "PRN2", "con-x.txt", "xcon", "CONIN", "CONOUT",
        "CO", "N", "A.CON", "a.nul",
    };
    check_segments_accepted(names, COUNT_OF(names));
}

static void non_ascii_names_are_unsafe(void)
{
    static const char *const names[] = {
        "\xD1\x82\xD0\xB5\xD1\x81\xD1\x82", "caf\xE9", "\xC3\xA9.sav", "a\x80", "\xE2\x82\xAC", "slot_\xD0\x96",
    };
    check_segments_rejected(names, COUNT_OF(names));
}

static void ordinary_save_file_names_are_safe(void)
{
    static const char *const names[] = {
        "option.sav", "slot_00", "caption.sav", "direct_file_save_related.sav", "B.dat", "a-b.dat", "a_b.dat", "CacheStorageKey.dat",
        "storage", "album", "picturebook", ".hidden", "a..b", "a b.sav", " leading", "x$y", "100%", "a+b=c", "(1)", "[x]", "a,b", "a;b",
        "a'b", "a&b", "a#b", "a@b", "a!b", "a~b", "a^b", "a`b", "a{b}",
    };
    check_segments_accepted(names, COUNT_OF(names));
}

static void a_segment_of_255_characters_is_safe_and_256_is_not(void)
{
    char path[300];
    memset(path, 'a', 255);
    path[255] = '\0';
    CHECK(save_rules_is_safe_path(path));
    memset(path, 'a', 256);
    path[256] = '\0';
    CHECK(!save_rules_is_safe_path(path));
}

static void a_path_is_safe_when_every_segment_is_safe(void)
{
    CHECK(save_rules_is_safe_path("option.sav"));
    CHECK(save_rules_is_safe_path("slot_00/progress.sav"));
    CHECK(save_rules_is_safe_path("picturebook/a/b.dat"));
    CHECK(save_rules_is_safe_path("a/b/c/d/e/f/g/h/i/j"));
}

static void a_path_with_one_unsafe_segment_anywhere_is_unsafe(void)
{
    static const char *const paths[] = {
        "", "/", "/evil.sav", "evil.sav/", "a//b", "../evil.sav", "slot_00/../evil.sav", "a/b/..", "./a", "a/./b", "slot_01\\progress.sav",
        "C:/evil.sav", "option.sav:stream", "slot_01/NUL", "slot_01/caption.sav.", "slot_01/caption.sav ", "album./x.dat", "slot_01/CON .txt",
        "slot_01/nul  .dat", "con/x.sav", "a/b/aux", "\xD1\x82/x.sav", "a/\xD1\x82",
    };
    for (size_t i = 0; i < COUNT_OF(paths); i++) {
        if (save_rules_is_safe_path(paths[i])) {
            printf("  accepted but must be rejected: \"%s\"\n", paths[i]);
        }
        CHECK(!save_rules_is_safe_path(paths[i]));
    }
}

static void every_unsafe_path_vector_of_the_pc_contract_is_rejected_too(void)
{
    static const char PREFIX[] = "unsafe path: ";
    cJSON *cases = test_load_cases();
    int checked = 0;
    CHECK(cases != NULL);
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, cases)
    {
        const cJSON *reason = cJSON_GetObjectItemCaseSensitive(item, "reason");
        if (cJSON_IsString(reason) && strncmp(reason->valuestring, PREFIX, strlen(PREFIX)) == 0) {
            const char *path = reason->valuestring + strlen(PREFIX);
            if (save_rules_is_safe_path(path)) {
                printf("  contract path \"%s\" is accepted by the Switch\n", path);
            }
            CHECK(!save_rules_is_safe_path(path));
            checked++;
        }
    }
    CHECK(checked >= 12);
    cJSON_Delete(cases);
}

static void every_saved_name_of_the_valid_contract_vectors_is_safe(void)
{
    cJSON *cases = test_load_cases();
    int checked = 0;
    CHECK(cases != NULL);
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, cases)
    {
        const char *lists[] = { "saved_files", "saved_dirs" };
        for (size_t i = 0; i < COUNT_OF(lists); i++) {
            const cJSON *name = NULL;
            cJSON_ArrayForEach(name, cJSON_GetObjectItemCaseSensitive(item, lists[i]))
            {
                CHECK(save_rules_is_safe_path(name->valuestring));
                checked++;
            }
        }
    }
    CHECK(checked >= 40);
    cJSON_Delete(cases);
}

static void junk_is_found_by_file_name_in_any_letter_case(void)
{
    static const char *const junk[] = {
        ".nx_save_meta.bin", ".NX_SAVE_META.BIN", "desktop.ini", "DESKTOP.INI", "Desktop.Ini", "Thumbs.db", "THUMBS.DB", "thumbs.db",
        "x.zip", "X.ZIP", "a.7z", "b.RAR", "c.Rar", "save.7Z", "picturebook/THUMBS.DB", "slot_00/.nx_save_meta.bin", "a/b/c/desktop.ini",
        "dir/backup.zip",
    };
    for (size_t i = 0; i < COUNT_OF(junk); i++) {
        if (!save_rules_is_junk(junk[i])) {
            printf("  not junk but must be: \"%s\"\n", junk[i]);
        }
        CHECK(save_rules_is_junk(junk[i]));
    }
}

static void real_save_files_are_not_junk(void)
{
    static const char *const kept[] = {
        "option.sav", "slot_00/progress.sav", "picturebook/a.dat", "storage/CacheStorageKey.dat", "zip", "azip", "a.zipx", "thumbs.db.bak",
        "desktop.ini.txt", "thumbs", "my.rar.sav", "nx_save_meta.bin", ".nx_save_meta.bin.old", "7z", "a.7zz", "", "desktop.ini/x.sav",
    };
    for (size_t i = 0; i < COUNT_OF(kept); i++) {
        if (save_rules_is_junk(kept[i])) {
            printf("  junk but must be kept: \"%s\"\n", kept[i]);
        }
        CHECK(!save_rules_is_junk(kept[i]));
    }
}

static void only_the_jksv_meta_file_counts_as_meta(void)
{
    CHECK(save_rules_is_meta(".nx_save_meta.bin"));
    CHECK(save_rules_is_meta(".NX_Save_Meta.BIN"));
    CHECK(save_rules_is_meta("slot_00/.nx_save_meta.bin"));
    CHECK(!save_rules_is_meta("desktop.ini"));
    CHECK(!save_rules_is_meta("x.zip"));
    CHECK(!save_rules_is_meta("option.sav"));
    CHECK(!save_rules_is_meta(".nx_save_meta.bin.old"));
}

void run_save_rules_tests(void)
{
    RUN_TEST(empty_and_dot_segments_are_unsafe);
    RUN_TEST(every_character_windows_forbids_makes_a_segment_unsafe);
    RUN_TEST(control_characters_and_delete_make_a_segment_unsafe);
    RUN_TEST(a_trailing_dot_or_space_makes_a_segment_unsafe);
    RUN_TEST(windows_device_names_are_unsafe_in_any_letter_case);
    RUN_TEST(a_device_name_with_an_extension_is_still_a_device);
    RUN_TEST(a_device_name_with_spaces_before_the_extension_is_still_a_device);
    RUN_TEST(names_that_only_look_like_devices_are_safe);
    RUN_TEST(non_ascii_names_are_unsafe);
    RUN_TEST(ordinary_save_file_names_are_safe);
    RUN_TEST(a_segment_of_255_characters_is_safe_and_256_is_not);
    RUN_TEST(a_path_is_safe_when_every_segment_is_safe);
    RUN_TEST(a_path_with_one_unsafe_segment_anywhere_is_unsafe);
    RUN_TEST(every_unsafe_path_vector_of_the_pc_contract_is_rejected_too);
    RUN_TEST(every_saved_name_of_the_valid_contract_vectors_is_safe);
    RUN_TEST(junk_is_found_by_file_name_in_any_letter_case);
    RUN_TEST(real_save_files_are_not_junk);
    RUN_TEST(only_the_jksv_meta_file_counts_as_meta);
}
