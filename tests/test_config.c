#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "fs_compat.h"
#include "paths.h"
#include "test.h"
#include "test_fs.h"

#define SECRET "S3cr3t-P@ssw0rd-XYZ"
#define TOTK_PATH "sd:/config/totk-sync/config.json"
#define JKSV_PATH "sd:/config/JKSV/webdav.json"
#define GAME TS_GAME_FOLDER

static const char *VALID_JSON =
    "{\"origin\":\"https://dav.example.com/remote.php\",\"basepath\":\"saves\","
    "\"username\":\"alice\",\"password\":\"" SECRET "\"}";

static config_status_t parse_totk(const char *json, config_t *cfg, config_error_t *err)
{
    return config_parse(json, CONFIG_SOURCE_TOTK_SYNC, TOTK_PATH, cfg, err);
}

static config_status_t parse_jksv(const char *json, config_t *cfg, config_error_t *err)
{
    return config_parse(json, CONFIG_SOURCE_JKSV, JKSV_PATH, cfg, err);
}

static void format_json(char *out, size_t size, const char *origin, const char *basepath, const char *username, const char *password)
{
    snprintf(out, size, "{\"origin\":\"%s\",\"basepath\":\"%s\",\"username\":\"%s\",\"password\":\"%s\"}", origin, basepath, username, password);
}

static void fill(char *out, size_t length)
{
    memset(out, 'a', length);
    out[length] = '\0';
}

static void parses_totk_sync_config_values_and_source(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(parse_totk(VALID_JSON, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.origin, "https://dav.example.com/remote.php");
    CHECK_STR_EQ(cfg.basepath, "saves");
    CHECK_STR_EQ(cfg.username, "alice");
    CHECK_STR_EQ(cfg.password, SECRET);
    CHECK_INT_EQ(cfg.source, CONFIG_SOURCE_TOTK_SYNC);
    CHECK_STR_EQ(cfg.source_display_path, TOTK_PATH);
}

static void accepts_and_ignores_keep_backups_field(void)
{
    config_t cfg;
    config_error_t err;
    const char *json = "{\"origin\":\"https://h\",\"basepath\":\"b\",\"username\":\"u\",\"password\":\"p\",\"keep_backups\":5}";
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.basepath, "b");
    CHECK_STR_EQ(cfg.username, "u");
}

static void totk_sync_basepath_is_not_extended_with_game_folder(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(parse_totk(VALID_JSON, &cfg, &err), CONFIG_OK);
    CHECK_STR_NOT_CONTAINS(cfg.basepath, GAME);
}

static void jksv_basepath_gets_game_folder_appended(void)
{
    config_t cfg;
    config_error_t err;
    char json[512];
    format_json(json, sizeof json, "https://h", "JKSV", "u", "p");
    CHECK_INT_EQ(parse_jksv(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.basepath, "JKSV/" GAME);
    CHECK_INT_EQ(cfg.source, CONFIG_SOURCE_JKSV);
    CHECK_STR_EQ(cfg.source_display_path, JKSV_PATH);
}

static void jksv_trailing_slashes_are_stripped_before_append(void)
{
    config_t cfg;
    config_error_t err;
    char json[512];
    format_json(json, sizeof json, "https://h", "JKSV///", "u", "p");
    CHECK_INT_EQ(parse_jksv(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.basepath, "JKSV/" GAME);
}

static void jksv_empty_basepath_yields_game_folder_only(void)
{
    config_t cfg;
    config_error_t err;
    char json[512];
    format_json(json, sizeof json, "https://h", "", "u", "p");
    CHECK_INT_EQ(parse_jksv(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.basepath, GAME);
}

static void jksv_slash_only_basepath_yields_game_folder_only(void)
{
    config_t cfg;
    config_error_t err;
    char json[512];
    format_json(json, sizeof json, "https://h", "/", "u", "p");
    CHECK_INT_EQ(parse_jksv(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.basepath, GAME);
}

static void jksv_missing_basepath_yields_game_folder_only(void)
{
    config_t cfg;
    config_error_t err;
    const char *json = "{\"origin\":\"https://h\",\"username\":\"u\",\"password\":\"p\"}";
    CHECK_INT_EQ(parse_jksv(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.basepath, GAME);
}

static void jksv_non_string_basepath_is_treated_as_missing(void)
{
    config_t cfg;
    config_error_t err;
    const char *json = "{\"origin\":\"https://h\",\"basepath\":42,\"username\":\"u\",\"password\":\"p\"}";
    CHECK_INT_EQ(parse_jksv(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.basepath, GAME);
}

static void jksv_basepath_overflowing_after_append_is_too_long(void)
{
    config_t cfg;
    config_error_t err;
    char base[512];
    char json[1024];
    fill(base, 500);
    format_json(json, sizeof json, "https://h", base, "u", SECRET);
    CHECK_INT_EQ(parse_jksv(json, &cfg, &err), CONFIG_ERR_FIELD_TOO_LONG);
    CHECK_INT_EQ(err.status, CONFIG_ERR_FIELD_TOO_LONG);
    CHECK(strstr(err.message, "basepath") != NULL);
    CHECK(strstr(err.message, JKSV_PATH) != NULL);
    CHECK_STR_NOT_CONTAINS(err.message, SECRET);
}

static void totk_sync_basepath_of_max_length_is_accepted(void)
{
    config_t cfg;
    config_error_t err;
    char base[512];
    char json[1024];
    fill(base, 511);
    format_json(json, sizeof json, "https://h", base, "u", "p");
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.basepath, base);
}

static void missing_required_field_names_field_and_path(void)
{
    static const char *const names[] = { "origin", "basepath", "username", "password" };
    for (int skip = 0; skip < 4; skip++) {
        char json[512] = "{";
        for (int i = 0; i < 4; i++) {
            if (i == skip) {
                continue;
            }
            char entry[160];
            snprintf(entry, sizeof entry, "%s\"%s\":\"%s\"", strlen(json) > 1 ? "," : "", names[i], i == 0 ? "https://h" : i == 3 ? SECRET : "x");
            strcat(json, entry);
        }
        strcat(json, "}");
        config_t cfg;
        config_error_t err;
        CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_ERR_FIELD);
        char quoted[32];
        snprintf(quoted, sizeof quoted, "\"%s\"", names[skip]);
        CHECK(strstr(err.message, quoted) != NULL);
        CHECK(strstr(err.message, TOTK_PATH) != NULL);
        CHECK_STR_NOT_CONTAINS(err.message, SECRET);
    }
}

static void empty_or_non_string_required_field_is_a_field_error(void)
{
    static const char *const names[] = { "origin", "basepath", "username", "password" };
    static const char *const bad_values[] = { "\"\"", "123", "null", "true", "[\"x\"]", "{}" };
    for (int field = 0; field < 4; field++) {
        for (size_t v = 0; v < sizeof bad_values / sizeof bad_values[0]; v++) {
            char json[512];
            snprintf(json, sizeof json, "{\"origin\":%s,\"basepath\":%s,\"username\":%s,\"password\":%s}",
                field == 0 ? bad_values[v] : "\"https://h\"",
                field == 1 ? bad_values[v] : "\"b\"",
                field == 2 ? bad_values[v] : "\"u\"",
                field == 3 ? bad_values[v] : "\"" SECRET "\"");
            config_t cfg;
            config_error_t err;
            CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_ERR_FIELD);
            char quoted[32];
            snprintf(quoted, sizeof quoted, "\"%s\"", names[field]);
            CHECK(strstr(err.message, quoted) != NULL);
            CHECK(strstr(err.message, TOTK_PATH) != NULL);
            CHECK_STR_NOT_CONTAINS(err.message, SECRET);
        }
    }
}

static void jksv_missing_origin_is_a_field_error_naming_jksv_path(void)
{
    config_t cfg;
    config_error_t err;
    const char *json = "{\"basepath\":\"b\",\"username\":\"u\",\"password\":\"" SECRET "\"}";
    CHECK_INT_EQ(parse_jksv(json, &cfg, &err), CONFIG_ERR_FIELD);
    CHECK(strstr(err.message, "\"origin\"") != NULL);
    CHECK(strstr(err.message, JKSV_PATH) != NULL);
    CHECK_STR_NOT_CONTAINS(err.message, SECRET);
}

static void non_object_json_reports_missing_fields(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(parse_totk("[1,2,3]", &cfg, &err), CONFIG_ERR_FIELD);
    CHECK_INT_EQ(parse_totk("\"text\"", &cfg, &err), CONFIG_ERR_FIELD);
}

static void too_long_value_reports_field_name_and_limit(void)
{
    static const struct {
        const char *name;
        size_t max;
    } limits[] = {
        { "origin", 255 }, { "basepath", 511 }, { "username", 127 }, { "password", 127 },
    };
    for (int field = 0; field < 4; field++) {
        char big[600];
        char json[2048];
        fill(big, limits[field].max + 1);
        if (field == 0) {
            memcpy(big, "https://", 8);
        }
        format_json(json, sizeof json, field == 0 ? big : "https://h",
            field == 1 ? big : "b", field == 2 ? big : "u", field == 3 ? big : SECRET);
        config_t cfg;
        config_error_t err;
        CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_ERR_FIELD_TOO_LONG);
        char quoted[32];
        snprintf(quoted, sizeof quoted, "\"%s\"", limits[field].name);
        CHECK(strstr(err.message, quoted) != NULL);
        CHECK(strstr(err.message, TOTK_PATH) != NULL);
        CHECK_STR_NOT_CONTAINS(err.message, SECRET);
        char maximum[16];
        snprintf(maximum, sizeof maximum, "%zu", limits[field].max);
        CHECK(strstr(err.message, maximum) != NULL);
    }
}

static void value_at_exact_maximum_length_is_accepted(void)
{
    config_t cfg;
    config_error_t err;
    char user[200];
    char json[512];
    fill(user, 127);
    format_json(json, sizeof json, "https://h", "b", user, "p");
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.username, user);
}

static void non_https_origins_are_rejected_as_insecure(void)
{
    static const char *const origins[] = {
        "http://dav.example.com", "ftp://dav.example.com", "HTTPS://dav.example.com",
        "Https://dav.example.com", "dav.example.com", " https://dav.example.com",
        "https:/dav.example.com", "http", "httpss://x", "ws://x",
    };
    for (size_t i = 0; i < sizeof origins / sizeof origins[0]; i++) {
        char json[512];
        config_t cfg;
        config_error_t err;
        format_json(json, sizeof json, origins[i], "b", "u", SECRET);
        CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_ERR_INSECURE_ORIGIN);
        CHECK_INT_EQ(err.status, CONFIG_ERR_INSECURE_ORIGIN);
        CHECK(strstr(err.message, TOTK_PATH) != NULL);
        CHECK_STR_NOT_CONTAINS(err.message, SECRET);
        CHECK_INT_EQ(parse_jksv(json, &cfg, &err), CONFIG_ERR_INSECURE_ORIGIN);
    }
}

static void https_origin_is_accepted(void)
{
    config_t cfg;
    config_error_t err;
    char json[512];
    format_json(json, sizeof json, "https://x", "b", "u", "p");
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_OK);
}

static void broken_json_reports_line_and_column(void)
{
    config_t cfg;
    config_error_t err;
    const char *json = "{\n  \"origin\": \"https://h\",\n  \"basepath\": ?\n}";
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_ERR_PARSE);
    CHECK_INT_EQ(err.status, CONFIG_ERR_PARSE);
    CHECK(strncmp(err.message, "Parse error in " TOTK_PATH, strlen("Parse error in " TOTK_PATH)) == 0);
    CHECK(strstr(err.message, "line 3, column 15") != NULL);
}

static void broken_json_on_first_line_reports_line_one(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(parse_jksv("{\"a\" 1}", &cfg, &err), CONFIG_ERR_PARSE);
    CHECK(strstr(err.message, "Parse error in " JKSV_PATH) == err.message);
    CHECK(strstr(err.message, "line 1, column 6") != NULL);
}

static void empty_input_is_a_parse_error_and_does_not_crash(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(parse_totk("", &cfg, &err), CONFIG_ERR_PARSE);
    CHECK(strstr(err.message, "Parse error in " TOTK_PATH) == err.message);
}

static void trailing_comma_object_is_rejected_without_leaking_password(void)
{
    config_t cfg;
    config_error_t err;
    char json[512];
    snprintf(json, sizeof json, "{\"origin\":\"https://h\",\"basepath\":\"b\",\"username\":\"u\",\"password\":\"%s\",}", SECRET);
    CHECK(parse_totk(json, &cfg, &err) != CONFIG_OK);
    CHECK_STR_NOT_CONTAINS(err.message, SECRET);
}

static void parse_error_message_never_contains_password(void)
{
    config_t cfg;
    config_error_t err;
    char json[512];
    snprintf(json, sizeof json, "{\"password\":\"%s\" \"origin\"", SECRET);
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_ERR_PARSE);
    CHECK_STR_NOT_CONTAINS(err.message, SECRET);
}

static void decodes_json_escapes_to_exact_bytes(void)
{
    config_t cfg;
    config_error_t err;
    const char *json = "{\"origin\":\"https:\\/\\/h\",\"basepath\":\"a\\\\b\","
                       "\"username\":\"q\\\"r\",\"password\":\"\\u043a\\u043e\\u0442\"}";
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.origin, "https://h");
    CHECK_STR_EQ(cfg.basepath, "a\\b");
    CHECK_STR_EQ(cfg.username, "q\"r");
    CHECK_STR_EQ(cfg.password, "\xD0\xBA\xD0\xBE\xD1\x82");
}

static void decodes_surrogate_pair_to_four_byte_utf8(void)
{
    config_t cfg;
    config_error_t err;
    const char *json = "{\"origin\":\"https://h\",\"basepath\":\"b\",\"username\":\"\\ud83d\\ude00\",\"password\":\"p\"}";
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_OK);
    CHECK_INT_EQ(strlen(cfg.username), 4);
    CHECK(memcmp(cfg.username, "\xF0\x9F\x98\x80", 5) == 0);
}

static void raw_utf8_text_is_preserved_byte_exact(void)
{
    config_t cfg;
    config_error_t err;
    const char *json = "{\"origin\":\"https://h\",\"basepath\":\"b\",\"username\":\"\xD0\xBA\xD0\xBE\xD1\x82\",\"password\":\"p\"}";
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.username, "\xD0\xBA\xD0\xBE\xD1\x82");
}

static void utf8_bom_at_start_is_accepted(void)
{
    config_t cfg;
    config_error_t err;
    char json[512];
    snprintf(json, sizeof json, "\xEF\xBB\xBF%s", VALID_JSON);
    CHECK_INT_EQ(parse_totk(json, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.username, "alice");
}

static void parse_error_position_ignores_bom(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(parse_totk("\xEF\xBB\xBF{\"a\" 1}", &cfg, &err), CONFIG_ERR_PARSE);
    CHECK(strstr(err.message, "line 1, column 6") != NULL);
}

static void successful_parse_clears_previous_error(void)
{
    config_t cfg;
    config_error_t err;
    parse_totk("{", &cfg, &err);
    CHECK_INT_EQ(parse_totk(VALID_JSON, &cfg, &err), CONFIG_OK);
    CHECK_INT_EQ(err.status, CONFIG_OK);
    CHECK_STR_EQ(err.message, "");
}

/* ---- config_load: file selection ---- */

static const char *TOTK_JSON =
    "{\"origin\":\"https://totk.example\",\"basepath\":\"fromtotk\",\"username\":\"tu\",\"password\":\"" SECRET "\"}";
static const char *JKSV_JSON =
    "{\"origin\":\"https://jksv.example\",\"basepath\":\"JKSV\",\"username\":\"ju\",\"password\":\"jp\"}";

static bool make_root(char *root, size_t size)
{
    bool ok = test_fs_make_root(root, size);
    CHECK(ok);
    return ok;
}

static void write_config(const char *root, const char *rel, const char *content)
{
    CHECK(test_fs_write_file(root, rel, content, strlen(content)));
}

static void load_uses_totk_sync_config_when_only_it_exists(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    write_config(root, TS_REL_CONFIG, TOTK_JSON);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.origin, "https://totk.example");
    CHECK_STR_EQ(cfg.basepath, "fromtotk");
    CHECK_INT_EQ(cfg.source, CONFIG_SOURCE_TOTK_SYNC);
    CHECK_STR_EQ(cfg.source_display_path, TOTK_PATH);
}

static void load_falls_back_to_jksv_config_when_totk_sync_missing(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    write_config(root, TS_REL_JKSV_CONFIG, JKSV_JSON);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.origin, "https://jksv.example");
    CHECK_STR_EQ(cfg.basepath, "JKSV/" GAME);
    CHECK_STR_EQ(cfg.username, "ju");
    CHECK_INT_EQ(cfg.source, CONFIG_SOURCE_JKSV);
    CHECK_STR_EQ(cfg.source_display_path, JKSV_PATH);
}

static void load_prefers_totk_sync_when_both_exist(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    write_config(root, TS_REL_CONFIG, TOTK_JSON);
    write_config(root, TS_REL_JKSV_CONFIG, JKSV_JSON);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.origin, "https://totk.example");
    CHECK_INT_EQ(cfg.source, CONFIG_SOURCE_TOTK_SYNC);
}

static void load_does_not_fall_back_when_totk_sync_config_is_broken(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    write_config(root, TS_REL_CONFIG, "{\n  broken");
    write_config(root, TS_REL_JKSV_CONFIG, JKSV_JSON);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_PARSE);
    CHECK(strstr(err.message, TOTK_PATH) != NULL);
    CHECK_STR_NOT_CONTAINS(err.message, JKSV_PATH);
}

static void load_does_not_fall_back_when_totk_sync_config_has_field_error(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    write_config(root, TS_REL_CONFIG, "{\"origin\":\"https://h\"}");
    write_config(root, TS_REL_JKSV_CONFIG, JKSV_JSON);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_FIELD);
    CHECK(strstr(err.message, TOTK_PATH) != NULL);
}

static void load_does_not_fall_back_when_totk_sync_origin_is_insecure(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    char json[512];
    if (!make_root(root, sizeof root)) return;
    format_json(json, sizeof json, "http://h", "b", "u", SECRET);
    write_config(root, TS_REL_CONFIG, json);
    write_config(root, TS_REL_JKSV_CONFIG, JKSV_JSON);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_INSECURE_ORIGIN);
    CHECK_STR_NOT_CONTAINS(err.message, SECRET);
}

static void load_reports_jksv_errors_with_jksv_path(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    write_config(root, TS_REL_JKSV_CONFIG, "{\"origin\":\"http://h\",\"username\":\"u\",\"password\":\"" SECRET "\"}");
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_INSECURE_ORIGIN);
    CHECK(strstr(err.message, JKSV_PATH) != NULL);
    CHECK_STR_NOT_CONTAINS(err.message, SECRET);
}

static void load_reports_not_found_naming_both_paths_when_neither_exists(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_NOT_FOUND);
    CHECK_INT_EQ(err.status, CONFIG_ERR_NOT_FOUND);
    CHECK(strstr(err.message, TOTK_PATH) != NULL);
    CHECK(strstr(err.message, JKSV_PATH) != NULL);
}

static void load_accepts_config_file_with_bom(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    char json[512];
    if (!make_root(root, sizeof root)) return;
    snprintf(json, sizeof json, "\xEF\xBB\xBF%s", TOTK_JSON);
    write_config(root, TS_REL_CONFIG, json);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.username, "tu");
}

static void load_treats_empty_file_as_parse_error_without_fallback(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    write_config(root, TS_REL_CONFIG, "");
    write_config(root, TS_REL_JKSV_CONFIG, JKSV_JSON);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_PARSE);
    CHECK(strstr(err.message, TOTK_PATH) != NULL);
}

/* Unreadable config is simulated by making config.json a directory: on Windows/MinGW fopen(dir, "rb")
   fails with EACCES (not ENOENT), the same "exists but cannot be read" class as an SD read error. */
static bool make_config_unreadable(const char *root, const char *rel)
{
    char path[512];
    char marker[256];
    snprintf(marker, sizeof marker, "%s.keep", rel);
    write_config(root, marker, "x");
    snprintf(path, sizeof path, "%s/%s", root, rel);
    return ts_mkdir(path) == 0;
}

static void load_reports_read_error_when_totk_sync_config_is_unreadable(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    CHECK(make_config_unreadable(root, TS_REL_CONFIG));
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_READ);
    CHECK(strstr(err.message, TOTK_PATH) != NULL);
}

static void load_does_not_fall_back_to_jksv_when_totk_sync_config_is_unreadable(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    CHECK(make_config_unreadable(root, TS_REL_CONFIG));
    write_config(root, TS_REL_JKSV_CONFIG, JKSV_JSON);
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_READ);
    CHECK_STR_NOT_CONTAINS(err.message, JKSV_PATH);
}

static void load_reports_read_error_naming_jksv_path_when_only_jksv_config_is_unreadable(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    if (!make_root(root, sizeof root)) return;
    CHECK(make_config_unreadable(root, TS_REL_JKSV_CONFIG));
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_READ);
    CHECK(strstr(err.message, JKSV_PATH) != NULL);
}

void run_config_tests(void)
{
    RUN_TEST(parses_totk_sync_config_values_and_source);
    RUN_TEST(accepts_and_ignores_keep_backups_field);
    RUN_TEST(totk_sync_basepath_is_not_extended_with_game_folder);
    RUN_TEST(jksv_basepath_gets_game_folder_appended);
    RUN_TEST(jksv_trailing_slashes_are_stripped_before_append);
    RUN_TEST(jksv_empty_basepath_yields_game_folder_only);
    RUN_TEST(jksv_slash_only_basepath_yields_game_folder_only);
    RUN_TEST(jksv_missing_basepath_yields_game_folder_only);
    RUN_TEST(jksv_non_string_basepath_is_treated_as_missing);
    RUN_TEST(jksv_basepath_overflowing_after_append_is_too_long);
    RUN_TEST(totk_sync_basepath_of_max_length_is_accepted);
    RUN_TEST(missing_required_field_names_field_and_path);
    RUN_TEST(empty_or_non_string_required_field_is_a_field_error);
    RUN_TEST(jksv_missing_origin_is_a_field_error_naming_jksv_path);
    RUN_TEST(non_object_json_reports_missing_fields);
    RUN_TEST(too_long_value_reports_field_name_and_limit);
    RUN_TEST(value_at_exact_maximum_length_is_accepted);
    RUN_TEST(non_https_origins_are_rejected_as_insecure);
    RUN_TEST(https_origin_is_accepted);
    RUN_TEST(broken_json_reports_line_and_column);
    RUN_TEST(broken_json_on_first_line_reports_line_one);
    RUN_TEST(empty_input_is_a_parse_error_and_does_not_crash);
    RUN_TEST(trailing_comma_object_is_rejected_without_leaking_password);
    RUN_TEST(parse_error_message_never_contains_password);
    RUN_TEST(decodes_json_escapes_to_exact_bytes);
    RUN_TEST(decodes_surrogate_pair_to_four_byte_utf8);
    RUN_TEST(raw_utf8_text_is_preserved_byte_exact);
    RUN_TEST(utf8_bom_at_start_is_accepted);
    RUN_TEST(parse_error_position_ignores_bom);
    RUN_TEST(successful_parse_clears_previous_error);
    RUN_TEST(load_uses_totk_sync_config_when_only_it_exists);
    RUN_TEST(load_falls_back_to_jksv_config_when_totk_sync_missing);
    RUN_TEST(load_prefers_totk_sync_when_both_exist);
    RUN_TEST(load_does_not_fall_back_when_totk_sync_config_is_broken);
    RUN_TEST(load_does_not_fall_back_when_totk_sync_config_has_field_error);
    RUN_TEST(load_does_not_fall_back_when_totk_sync_origin_is_insecure);
    RUN_TEST(load_reports_jksv_errors_with_jksv_path);
    RUN_TEST(load_reports_not_found_naming_both_paths_when_neither_exists);
    RUN_TEST(load_accepts_config_file_with_bom);
    RUN_TEST(load_treats_empty_file_as_parse_error_without_fallback);
    RUN_TEST(load_reports_read_error_when_totk_sync_config_is_unreadable);
    RUN_TEST(load_does_not_fall_back_to_jksv_when_totk_sync_config_is_unreadable);
    RUN_TEST(load_reports_read_error_naming_jksv_path_when_only_jksv_config_is_unreadable);
}
