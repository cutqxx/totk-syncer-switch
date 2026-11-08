#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "test.h"
#include "test_support.h"
#include "text.h"

/* ---- ASCII guard: the libnx console font has no Cyrillic glyphs ---- */

static bool has_non_ascii_byte(const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        if (*p > 0x7E || (*p < 0x20 && *p != '\n' && *p != '\r' && *p != '\t')) {
            return true;
        }
    }
    return false;
}

static void text_header_contains_only_printable_ascii(void)
{
    char *text = test_read_file_in(SOURCE_DIR "/core", "text.h");
    CHECK(text != NULL);
    CHECK(!has_non_ascii_byte(text));
    free(text);
}

static void every_screen_and_log_string_is_printable_ascii(void)
{
    static const char *const strings[] = {
        TXT_TITLE, TXT_LABEL_CONFIG, TXT_LABEL_PC_ADDRESS, TXT_BUTTONS_READY, TXT_BUTTONS_BLOCKED,
        TXT_STATUS_CHECKING, TXT_STATUS_ONLINE_SAVE, TXT_STATUS_ONLINE_NO_SAVE, TXT_STATUS_GAME_RUNNING,
        TXT_STATUS_NOT_REACHABLE, TXT_STATUS_ERROR, TXT_MSG_NOT_REACHABLE, TXT_MSG_WRONG_TOKEN, TXT_MSG_NOT_LAUNCHER,
        TXT_MSG_FORBIDDEN, TXT_MSG_HTTP_ERROR, TXT_MSG_BAD_RESPONSE, TXT_MSG_NETWORK_OTHER, TXT_RETRYING,
        TXT_LOG_UNAVAILABLE, TXT_SOCKET_INIT_FAILED, TXT_CURL_INIT_FAILED, TXT_CONFIG_NOT_FOUND, TXT_CONFIG_READ_ERROR,
        TXT_CONFIG_PARSE_ERROR, TXT_CONFIG_PARSE_ERROR_AT, TXT_CONFIG_FIELD_ERROR, TXT_CONFIG_FIELD_TOO_LONG,
        TXT_CONFIG_LEGACY_PHASE0, TXT_CONFIG_NOT_PRIVATE_IPV4, TXT_CONFIG_PORT_INVALID, TXT_CONFIG_TOKEN_INVALID,
        TXT_CONFIG_KEEP_BACKUPS_INVALID, TXT_SEND_TITLE, TXT_STEP_CHECK_GAME, TXT_STEP_READING_SAVE, TXT_STEP_SAVE_READ,
        TXT_STEP_ZIP_READY, TXT_STEP_SENDING, TXT_STEP_WAIT, TXT_RESULT_SENT, TXT_ACCOUNT_MENU_TITLE, TXT_ACCOUNT_MENU_BUTTONS,
        TXT_MSG_CLOSE_TOTK, TXT_MSG_GAME_CHECK_FAILED, TXT_MSG_NO_SAVE_ON_CONSOLE, TXT_MSG_SEND_CANCELLED, TXT_MSG_MOUNT_FAILED,
        TXT_MSG_SAVE_TOO_LARGE, TXT_MSG_TOO_MANY_ENTRIES, TXT_MSG_BAD_ENTRY_NAME, TXT_MSG_SAVE_READ_FAILED, TXT_MSG_ZIP_FAILED,
        TXT_MSG_PC_REJECTED, TXT_MSG_PC_GAME_RUNNING, TXT_MSG_PC_BUSY, TXT_MSG_PC_NO_SAVE, TXT_MSG_PC_WRITE_FAILED,
        TXT_MSG_SEND_UNCERTAIN, TXT_MSG_BAD_PUT_RESPONSE,
    };
    for (size_t i = 0; i < sizeof strings / sizeof strings[0]; i++) {
        CHECK(test_is_ascii_text(strings[i]));
    }
}

static void every_config_error_message_is_printable_ascii(void)
{
    static const char *const broken[] = {
        "", "{", "{}", "[]", "{\"origin\":\"x\"}",
        "{\"pc_host\":\"8.8.8.8\",\"pc_port\":8765,\"token\":\"t\"}",
        "{\"pc_host\":\"10.0.0.1\",\"pc_port\":1,\"token\":\"t\"}",
        "{\"pc_host\":\"10.0.0.1\",\"pc_port\":8765,\"token\":\"a b\"}",
        "{\"pc_host\":\"10.0.0.1\",\"pc_port\":8765,\"token\":\"t\",\"keep_backups\":0}",
        "{\"pc_host\":\"10.0.0.1\",\"pc_port\":8765}",
    };
    for (size_t i = 0; i < sizeof broken / sizeof broken[0]; i++) {
        config_t cfg;
        config_error_t err;
        CHECK(config_parse(broken[i], "sd:/config/totk-sync/config.json", &cfg, &err) != CONFIG_OK);
        CHECK(test_is_ascii_text(err.message));
    }
}

/* ---- repo scans over the Switch app source ---- */

static const char *const SOURCE_DIRS[] = { SOURCE_DIR, SOURCE_DIR "/core", SOURCE_DIR "/platform" };

static char lower_of(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool contains_ignoring_case(const char *haystack, const char *needle)
{
    size_t length = strlen(needle);
    for (const char *start = haystack; *start != '\0'; start++) {
        size_t i = 0;
        while (i < length && start[i] != '\0' && lower_of(start[i]) == lower_of(needle[i])) {
            i++;
        }
        if (i == length) {
            return true;
        }
    }
    return false;
}

static bool is_source_file_name(const char *name)
{
    size_t length = strlen(name);
    return length > 2 && name[length - 2] == '.' && (name[length - 1] == 'c' || name[length - 1] == 'h');
}

static bool is_offender_printing_on = true;

/* Counts source files in dir whose text contains needle (case-insensitive); prints each offender. */
static int count_files_containing(const char *dir, const char *needle)
{
    int hits = 0;
    DIR *handle = opendir(dir);
    CHECK(handle != NULL);
    if (handle == NULL) {
        return 0;
    }
    for (struct dirent *entry = readdir(handle); entry != NULL; entry = readdir(handle)) {
        if (!is_source_file_name(entry->d_name)) {
            continue;
        }
        char *text = test_read_file_in(dir, entry->d_name);
        if (text != NULL && contains_ignoring_case(text, needle)) {
            if (is_offender_printing_on) {
                printf("  %s/%s contains \"%s\"\n", dir, entry->d_name, needle);
            }
            hits++;
        }
        free(text);
    }
    closedir(handle);
    return hits;
}

static int count_source_files_containing(const char *needle)
{
    int hits = 0;
    for (size_t i = 0; i < sizeof SOURCE_DIRS / sizeof SOURCE_DIRS[0]; i++) {
        hits += count_files_containing(SOURCE_DIRS[i], needle);
    }
    return hits;
}

static void app_source_is_plain_ascii_everywhere(void)
{
    int offenders = 0;
    for (size_t d = 0; d < sizeof SOURCE_DIRS / sizeof SOURCE_DIRS[0]; d++) {
        DIR *handle = opendir(SOURCE_DIRS[d]);
        CHECK(handle != NULL);
        for (struct dirent *entry = handle ? readdir(handle) : NULL; entry != NULL; entry = readdir(handle)) {
            if (!is_source_file_name(entry->d_name)) {
                continue;
            }
            char *text = test_read_file_in(SOURCE_DIRS[d], entry->d_name);
            if (text != NULL && has_non_ascii_byte(text)) {
                printf("  non-ASCII byte in %s/%s\n", SOURCE_DIRS[d], entry->d_name);
                offenders++;
            }
            free(text);
        }
        if (handle != NULL) {
            closedir(handle);
        }
    }
    CHECK_INT_EQ(offenders, 0);
}

static void app_never_disables_tls_verification(void)
{
    CHECK_INT_EQ(count_source_files_containing("CURLOPT_SSL_VERIFYPEER"), 0);
    CHECK_INT_EQ(count_source_files_containing("CURLOPT_SSL_VERIFYHOST"), 0);
    CHECK_INT_EQ(count_source_files_containing("VERIFYPEER"), 0);
    CHECK_INT_EQ(count_source_files_containing("VERIFYHOST"), 0);
    CHECK_INT_EQ(count_source_files_containing("CURLOPT_PROXY_SSL"), 0);
}

static void phase0_yandex_webdav_and_experiments_are_gone_from_the_app(void)
{
    CHECK_INT_EQ(count_source_files_containing("webdav"), 0);
    CHECK_INT_EQ(count_source_files_containing("yandex"), 0);
    CHECK_INT_EQ(count_source_files_containing("JKSV"), 0);
    CHECK_INT_EQ(count_source_files_containing("test.zip"), 0);
    CHECK_INT_EQ(count_source_files_containing("https://"), 0);
}

static void curl_adapter_allows_only_plain_http_and_no_redirects(void)
{
    char *adapter = test_read_file_in(SOURCE_DIR "/platform", "http_curl.c");
    CHECK(adapter != NULL);
    CHECK(strstr(adapter, "CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTP)") != NULL);
    CHECK(strstr(adapter, "CURLPROTO_HTTPS") == NULL);
    CHECK(strstr(adapter, "CURLPROTO_ALL") == NULL);
    CHECK(strstr(adapter, "CURLOPT_FOLLOWLOCATION, 0L") != NULL);
    CHECK(strstr(adapter, "CURLOPT_CONNECTTIMEOUT, req->connect_timeout_s") != NULL);
    CHECK(strstr(adapter, "CURLOPT_TIMEOUT, req->total_timeout_s") != NULL);
    free(adapter);
}

static void every_text_macro_is_in_the_ascii_guard_list(void)
{
    char *header = test_read_file_in(SOURCE_DIR "/core", "text.h");
    char *guard = test_read_file("test_text_and_repo_rules.c");
    int checked = 0;
    CHECK(header != NULL);
    CHECK(guard != NULL);
    for (const char *at = header != NULL ? strstr(header, "#define TXT_") : NULL; at != NULL; at = strstr(at + 1, "#define TXT_")) {
        char name[96];
        size_t length = strspn(at + strlen("#define "), "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
        snprintf(name, sizeof name, "%.*s,", (int)length, at + strlen("#define "));
        if (guard != NULL && strstr(guard, name) == NULL) {
            printf("  %.*s is missing from every_screen_and_log_string_is_printable_ascii\n", (int)length - 0, name);
        }
        CHECK(guard != NULL && strstr(guard, name) != NULL);
        checked++;
    }
    CHECK(checked >= 50);
    free(header);
    free(guard);
}

static void the_unused_send_not_available_text_is_gone(void)
{
    CHECK_INT_EQ(count_source_files_containing("TXT_SEND_NOT_AVAILABLE"), 0);
    CHECK_INT_EQ(count_source_files_containing("Send to PC is not available"), 0);
}

static bool file_contains(const char *dir, const char *name, const char *needle)
{
    char *text = test_read_file_in(dir, name);
    bool is_found = text != NULL && strstr(text, needle) != NULL;
    CHECK(text != NULL);
    free(text);
    return is_found;
}

/* Counts "fsdevMountSaveData" in the app source that is not the read-only variant. */
static int count_writable_save_mounts(void)
{
    int offenders = 0;
    for (size_t d = 0; d < sizeof SOURCE_DIRS / sizeof SOURCE_DIRS[0]; d++) {
        DIR *handle = opendir(SOURCE_DIRS[d]);
        CHECK(handle != NULL);
        for (struct dirent *entry = handle ? readdir(handle) : NULL; entry != NULL; entry = readdir(handle)) {
            if (!is_source_file_name(entry->d_name)) {
                continue;
            }
            char *text = test_read_file_in(SOURCE_DIRS[d], entry->d_name);
            for (const char *at = text != NULL ? strstr(text, "fsdevMountSaveData") : NULL; at != NULL;
                 at = strstr(at + 1, "fsdevMountSaveData")) {
                if (strncmp(at, "fsdevMountSaveDataReadOnly", strlen("fsdevMountSaveDataReadOnly")) != 0) {
                    printf("  writable save mount in %s/%s\n", SOURCE_DIRS[d], entry->d_name);
                    offenders++;
                }
            }
            free(text);
        }
        if (handle != NULL) {
            closedir(handle);
        }
    }
    return offenders;
}

static void the_app_never_commits_to_the_file_system(void)
{
    CHECK_INT_EQ(count_source_files_containing("fsdevCommitDevice"), 0);
    CHECK_INT_EQ(count_source_files_containing("fsFsCommit"), 0);
    CHECK_INT_EQ(count_source_files_containing("fsdevCommit"), 0);
}

static void the_save_is_only_ever_mounted_read_only(void)
{
    CHECK_INT_EQ(count_writable_save_mounts(), 0);
    CHECK_INT_EQ(count_source_files_containing("fsOpenSaveDataFileSystem"), 0);
    CHECK(file_contains(SOURCE_DIR "/platform", "save_fs.c", "fsdevMountSaveDataReadOnly("));
}

static void the_code_that_touches_the_save_has_no_write_calls(void)
{
    static const char *const write_needles[] = {
        "\"wb\"", "\"w\"", "\"ab\"", "\"a\"", "\"r+\"", "\"rb+\"", "\"w+\"", "fwrite(", "fputs(", "fprintf(", "remove(", "rename(",
        "unlink(", "mkdir(", "rmdir(", "truncate(", "ftruncate(", "fsFsCreate", "fsFsWrite", "fsFsDelete", "fsFsRename",
    };
    static const struct {
        const char *dir;
        const char *name;
    } save_readers[] = {
        { SOURCE_DIR "/core", "save_pack.c" },
        { SOURCE_DIR "/core", "save_rules.c" },
        { SOURCE_DIR "/core", "account_pick.c" },
        { SOURCE_DIR "/platform", "save_fs.c" },
        { SOURCE_DIR "/platform", "system_nx.c" },
    };
    for (size_t r = 0; r < sizeof save_readers / sizeof save_readers[0]; r++) {
        for (size_t n = 0; n < sizeof write_needles / sizeof write_needles[0]; n++) {
            if (file_contains(save_readers[r].dir, save_readers[r].name, write_needles[n])) {
                printf("  %s uses %s\n", save_readers[r].name, write_needles[n]);
            }
            CHECK(!file_contains(save_readers[r].dir, save_readers[r].name, write_needles[n]));
        }
    }
    CHECK(file_contains(SOURCE_DIR "/core", "save_pack.c", "fopen(full_path, \"rb\")"));
}

static void the_curl_adapter_applies_the_100_continue_wait(void)
{
    CHECK(file_contains(SOURCE_DIR "/platform", "http_curl.c", "CURLOPT_EXPECT_100_TIMEOUT_MS"));
    CHECK(file_contains(SOURCE_DIR "/platform", "http_curl.c", "req->expect_100_timeout_ms"));
}

static void miniz_comes_only_from_third_party_with_its_license(void)
{
    char *license = test_read_file_in(SOURCE_DIR "/../third_party/miniz", "LICENSE");
    CHECK(license != NULL);
    CHECK(license != NULL && strstr(license, "Permission is hereby granted") != NULL);
    free(license);
    CHECK(file_contains(SOURCE_DIR "/../third_party/miniz", "miniz.h", "MZ_VERSION"));
    for (size_t i = 0; i < sizeof SOURCE_DIRS / sizeof SOURCE_DIRS[0]; i++) {
        char *copy_c = test_read_file_in(SOURCE_DIRS[i], "miniz.c");
        char *copy_h = test_read_file_in(SOURCE_DIRS[i], "miniz.h");
        CHECK(copy_c == NULL);
        CHECK(copy_h == NULL);
        free(copy_c);
        free(copy_h);
    }
    is_offender_printing_on = false;
    CHECK_INT_EQ(count_source_files_containing("#include \"miniz"), 1);
    is_offender_printing_on = true;
    CHECK(file_contains(SOURCE_DIR "/core", "zip_writer.c", "#include \"miniz.h\""));
}

static void both_makefiles_build_miniz_from_third_party_without_zlib_names(void)
{
    char *app = test_read_file_in(SOURCE_DIR "/..", "Makefile");
    char *tests = test_read_file("Makefile");
    CHECK(app != NULL && strstr(app, "third_party/miniz") != NULL);
    CHECK(app != NULL && strstr(app, "-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES") != NULL);
    CHECK(tests != NULL && strstr(tests, "../third_party/miniz") != NULL);
    CHECK(tests != NULL && strstr(tests, "-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES") != NULL);
    free(app);
    free(tests);
}

void run_text_and_repo_rule_tests(void)
{
    RUN_TEST(text_header_contains_only_printable_ascii);
    RUN_TEST(every_screen_and_log_string_is_printable_ascii);
    RUN_TEST(every_config_error_message_is_printable_ascii);
    RUN_TEST(app_source_is_plain_ascii_everywhere);
    RUN_TEST(app_never_disables_tls_verification);
    RUN_TEST(phase0_yandex_webdav_and_experiments_are_gone_from_the_app);
    RUN_TEST(curl_adapter_allows_only_plain_http_and_no_redirects);
    RUN_TEST(every_text_macro_is_in_the_ascii_guard_list);
    RUN_TEST(the_unused_send_not_available_text_is_gone);
    RUN_TEST(the_app_never_commits_to_the_file_system);
    RUN_TEST(the_save_is_only_ever_mounted_read_only);
    RUN_TEST(the_code_that_touches_the_save_has_no_write_calls);
    RUN_TEST(the_curl_adapter_applies_the_100_continue_wait);
    RUN_TEST(miniz_comes_only_from_third_party_with_its_license);
    RUN_TEST(both_makefiles_build_miniz_from_third_party_without_zlib_names);
}
