#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "test.h"
#include "test_fs.h"
#include "test_support.h"

#define DISPLAY_PATH "sd:/config/totk-sync/config.json"
#define GOOD_HOST "\"192.168.1.10\""
#define GOOD_PORT "8765"
#define GOOD_TOKEN "\"abcDEF123-_.~token\""
#define LEGACY_TEXT "Old Phase 0 config: save switch-config.json from the PC launcher"

/* Each argument is a raw JSON value; NULL leaves the field out. */
static void build_config(char *out, size_t size, const char *host, const char *port, const char *token, const char *keep)
{
    out[0] = '\0';
    size_t used = 0;
    used += (size_t)snprintf(out + used, size - used, "{\"unused\": 0");
    if (host != NULL) used += (size_t)snprintf(out + used, size - used, ",\"pc_host\":%s", host);
    if (port != NULL) used += (size_t)snprintf(out + used, size - used, ",\"pc_port\":%s", port);
    if (token != NULL) used += (size_t)snprintf(out + used, size - used, ",\"token\":%s", token);
    if (keep != NULL) used += (size_t)snprintf(out + used, size - used, ",\"keep_backups\":%s", keep);
    snprintf(out + used, size - used, "}");
}

static config_status_t parse_fields(const char *host, const char *port, const char *token, const char *keep,
                                    config_t *cfg, config_error_t *err)
{
    char json[1024];
    build_config(json, sizeof json, host, port, token, keep);
    return config_parse(json, DISPLAY_PATH, cfg, err);
}

static void accepts_the_contract_example_file_as_is(void)
{
    char *json = test_read_file_in(CONTRACTS_DIR, "switch-config.example.json");
    config_t cfg;
    config_error_t err;
    CHECK(json != NULL);
    CHECK_INT_EQ(config_parse(json, DISPLAY_PATH, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.pc_host, "192.168.1.10");
    CHECK_INT_EQ(cfg.pc_port, 8765);
    CHECK_STR_EQ(cfg.token, CONTRACT_TOKEN);
    CHECK_INT_EQ(cfg.keep_backups, 20);
    CHECK_STR_EQ(cfg.source_display_path, DISPLAY_PATH);
    free(json);
}

static void accepts_the_contract_example_with_crlf_line_endings(void)
{
    char *json = test_read_file_in(CONTRACTS_DIR, "switch-config.example.json");
    char crlf[1024];
    size_t used = 0;
    config_t cfg;
    config_error_t err;
    CHECK(json != NULL);
    for (const char *p = json; *p != '\0' && used + 2 < sizeof crlf; p++) {
        if (*p == '\n') crlf[used++] = '\r';
        crlf[used++] = *p;
    }
    crlf[used] = '\0';
    CHECK(strstr(crlf, "\r\n") != NULL);
    CHECK_INT_EQ(config_parse(crlf, DISPLAY_PATH, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.token, CONTRACT_TOKEN);
    free(json);
}

static void accepts_the_contract_example_with_utf8_bom(void)
{
    char *json = test_read_file_in(CONTRACTS_DIR, "switch-config.example.json");
    char with_bom[1024] = "\xEF\xBB\xBF";
    config_t cfg;
    config_error_t err;
    CHECK(json != NULL);
    strncat(with_bom, json, sizeof with_bom - 4);
    CHECK_INT_EQ(config_parse(with_bom, DISPLAY_PATH, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.pc_host, "192.168.1.10");
    free(json);
}

static void reports_each_missing_required_field_by_name_and_path(void)
{
    static const char *const names[] = { "pc_host", "pc_port", "token" };
    for (int missing = 0; missing < 3; missing++) {
        config_t cfg;
        config_error_t err;
        config_status_t status = parse_fields(missing == 0 ? NULL : GOOD_HOST, missing == 1 ? NULL : GOOD_PORT,
                                              missing == 2 ? NULL : GOOD_TOKEN, NULL, &cfg, &err);
        CHECK_INT_EQ(status, CONFIG_ERR_FIELD);
        CHECK_INT_EQ(err.status, CONFIG_ERR_FIELD);
        CHECK(strstr(err.message, names[missing]) != NULL);
        CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
    }
}

static void reports_empty_and_blank_text_fields_by_name_and_path(void)
{
    static const char *const empties[] = { "\"\"", "\"   \"", "\"\\t\\r\\n\"", "null", "5" };
    for (size_t i = 0; i < sizeof empties / sizeof empties[0]; i++) {
        config_t cfg;
        config_error_t err;
        CHECK_INT_EQ(parse_fields(empties[i], GOOD_PORT, GOOD_TOKEN, NULL, &cfg, &err), CONFIG_ERR_FIELD);
        CHECK(strstr(err.message, "pc_host") != NULL);
        CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
        CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, empties[i], NULL, &cfg, &err), CONFIG_ERR_FIELD);
        CHECK(strstr(err.message, "token") != NULL);
        CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
    }
}

static void rejects_a_token_with_a_space_or_control_or_non_ascii_character(void)
{
    static const char *const bad[] = {
        "\"abc def\"", "\"abc\\u0001def\"", "\"abc\\tdef\"", "\"abc\\u007fdef\"", "\"abc\\u00e9def\"",
        "\"\\u0442\\u043e\\u043a\\u0435\\u043d\"",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        config_t cfg;
        config_error_t err;
        CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, bad[i], NULL, &cfg, &err), CONFIG_ERR_INVALID_VALUE);
        CHECK(strstr(err.message, "token") != NULL);
        CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
    }
}

static void error_for_a_bad_token_never_contains_the_token(void)
{
    config_t cfg;
    config_error_t err;
    parse_fields(GOOD_HOST, GOOD_PORT, "\"secret value\"", NULL, &cfg, &err);
    CHECK_STR_NOT_CONTAINS(err.message, "secret");
}

static void make_quoted_token(char *out, size_t length)
{
    out[0] = '"';
    memset(out + 1, 'x', length);
    out[length + 1] = '"';
    out[length + 2] = '\0';
}

static void token_length_limit_is_127_characters(void)
{
    char token127[130];
    char token128[131];
    char token500[505];
    config_t cfg;
    config_error_t err;
    make_quoted_token(token127, 127);
    make_quoted_token(token128, 128);
    make_quoted_token(token500, 500);

    CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, token127, NULL, &cfg, &err), CONFIG_OK);
    CHECK_INT_EQ((long long)strlen(cfg.token), 127);
    CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, token128, NULL, &cfg, &err), CONFIG_ERR_FIELD_TOO_LONG);
    CHECK(strstr(err.message, "token") != NULL);
    CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
    CHECK(strstr(err.message, "127") != NULL);
    CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, token500, NULL, &cfg, &err), CONFIG_ERR_FIELD_TOO_LONG);
}

static void port_boundaries_are_1024_to_65535(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(parse_fields(GOOD_HOST, "1023", GOOD_TOKEN, NULL, &cfg, &err), CONFIG_ERR_INVALID_VALUE);
    CHECK(strstr(err.message, "pc_port") != NULL);
    CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
    CHECK_INT_EQ(parse_fields(GOOD_HOST, "1024", GOOD_TOKEN, NULL, &cfg, &err), CONFIG_OK);
    CHECK_INT_EQ(cfg.pc_port, 1024);
    CHECK_INT_EQ(parse_fields(GOOD_HOST, "65535", GOOD_TOKEN, NULL, &cfg, &err), CONFIG_OK);
    CHECK_INT_EQ(cfg.pc_port, 65535);
    CHECK_INT_EQ(parse_fields(GOOD_HOST, "65536", GOOD_TOKEN, NULL, &cfg, &err), CONFIG_ERR_INVALID_VALUE);
}

static void rejects_ports_that_are_not_whole_numbers(void)
{
    static const char *const bad[] = { "\"8765\"", "8765.5", "-8765", "0", "true", "null", "[8765]", "\"\"", "1e9" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        config_t cfg;
        config_error_t err;
        CHECK_INT_EQ(parse_fields(GOOD_HOST, bad[i], GOOD_TOKEN, NULL, &cfg, &err), CONFIG_ERR_INVALID_VALUE);
        CHECK(strstr(err.message, "pc_port") != NULL);
    }
}

static void keep_backups_defaults_to_20_when_absent(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, GOOD_TOKEN, NULL, &cfg, &err), CONFIG_OK);
    CHECK_INT_EQ(cfg.keep_backups, 20);
}

static void keep_backups_accepts_one_and_larger_whole_numbers(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, GOOD_TOKEN, "1", &cfg, &err), CONFIG_OK);
    CHECK_INT_EQ(cfg.keep_backups, 1);
    CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, GOOD_TOKEN, "50", &cfg, &err), CONFIG_OK);
    CHECK_INT_EQ(cfg.keep_backups, 50);
}

static void keep_backups_rejects_zero_negative_fractional_and_non_numbers(void)
{
    static const char *const bad[] = { "0", "-1", "1.5", "\"5\"", "null", "true", "99999999999" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        config_t cfg;
        config_error_t err;
        CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, GOOD_TOKEN, bad[i], &cfg, &err), CONFIG_ERR_INVALID_VALUE);
        CHECK(strstr(err.message, "keep_backups") != NULL);
        CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
    }
}

static void rejects_public_loopback_link_local_and_named_hosts(void)
{
    static const char *const bad[] = {
        "\"8.8.8.8\"", "\"127.0.0.1\"", "\"169.254.1.1\"", "\"172.32.0.1\"", "\"example.com\"",
        "\"192.168.001.10\"", "\"192.168.1.10 \"", "\"192.168.1.10:8765\"", "\"http://192.168.1.10\"",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        config_t cfg;
        config_error_t err;
        CHECK_INT_EQ(parse_fields(bad[i], GOOD_PORT, GOOD_TOKEN, NULL, &cfg, &err), CONFIG_ERR_NOT_PRIVATE_IPV4);
        CHECK(strstr(err.message, "pc_host") != NULL);
        CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
    }
}

static void accepts_private_hosts_including_the_longest_one(void)
{
    static const char *const good[] = { "10.0.0.1", "172.16.0.1", "172.31.255.255", "192.168.100.100", "192.168.255.255" };
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        char quoted[32];
        config_t cfg;
        config_error_t err;
        snprintf(quoted, sizeof quoted, "\"%s\"", good[i]);
        CHECK_INT_EQ(parse_fields(quoted, GOOD_PORT, GOOD_TOKEN, NULL, &cfg, &err), CONFIG_OK);
        CHECK_STR_EQ(cfg.pc_host, good[i]);
    }
}

static void phase0_yandex_config_gets_the_exact_legacy_message(void)
{
    const char *json = "{\"origin\":\"https://webdav.example\",\"username\":\"u\",\"password\":\"p\",\"basepath\":\"/x\"}";
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(config_parse(json, DISPLAY_PATH, &cfg, &err), CONFIG_ERR_LEGACY_PHASE0);
    CHECK_INT_EQ(err.status, CONFIG_ERR_LEGACY_PHASE0);
    CHECK_STR_EQ(err.message, LEGACY_TEXT);
}

static void a_single_phase0_field_is_enough_to_report_legacy(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(config_parse("{\"origin\":\"x\"}", DISPLAY_PATH, &cfg, &err), CONFIG_ERR_LEGACY_PHASE0);
    CHECK_STR_EQ(err.message, LEGACY_TEXT);
    CHECK_INT_EQ(config_parse("{\"password\":\"x\"}", DISPLAY_PATH, &cfg, &err), CONFIG_ERR_LEGACY_PHASE0);
}

static void legacy_message_does_not_leak_phase0_password(void)
{
    config_t cfg;
    config_error_t err;
    config_parse("{\"origin\":\"o\",\"username\":\"u\",\"password\":\"HunterTwo\"}", DISPLAY_PATH, &cfg, &err);
    CHECK_STR_NOT_CONTAINS(err.message, "HunterTwo");
}

static void new_fields_next_to_leftover_phase0_fields_are_a_normal_config(void)
{
    const char *json = "{\"origin\":\"x\",\"pc_host\":\"10.0.0.2\",\"pc_port\":8765,\"token\":\"tok\"}";
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(config_parse(json, DISPLAY_PATH, &cfg, &err), CONFIG_OK);
}

static void an_empty_object_is_a_missing_field_not_legacy(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(config_parse("{}", DISPLAY_PATH, &cfg, &err), CONFIG_ERR_FIELD);
    CHECK(strstr(err.message, "pc_host") != NULL);
}

static void broken_json_reports_parse_error_with_path_and_position(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(config_parse("{\n  \"pc_host\": ,\n}", DISPLAY_PATH, &cfg, &err), CONFIG_ERR_PARSE);
    CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
    CHECK(strstr(err.message, "line 2") != NULL);
}

static void empty_or_non_object_json_is_rejected_without_crashing(void)
{
    config_t cfg;
    config_error_t err;
    CHECK_INT_EQ(config_parse("", DISPLAY_PATH, &cfg, &err), CONFIG_ERR_PARSE);
    CHECK_INT_EQ(config_parse("{\"pc_host\":", DISPLAY_PATH, &cfg, &err), CONFIG_ERR_PARSE);
    CHECK_INT_EQ(config_parse("[]", DISPLAY_PATH, &cfg, &err), CONFIG_ERR_FIELD);
    CHECK_INT_EQ(config_parse("\"text\"", DISPLAY_PATH, &cfg, &err), CONFIG_ERR_FIELD);
    CHECK_INT_EQ(config_parse("null", DISPLAY_PATH, &cfg, &err), CONFIG_ERR_FIELD);
}

static void successful_parse_clears_the_error(void)
{
    config_t cfg;
    config_error_t err;
    snprintf(err.message, sizeof err.message, "stale");
    err.status = CONFIG_ERR_PARSE;
    CHECK_INT_EQ(parse_fields(GOOD_HOST, GOOD_PORT, GOOD_TOKEN, NULL, &cfg, &err), CONFIG_OK);
    CHECK_INT_EQ(err.status, CONFIG_OK);
    CHECK_STR_EQ(err.message, "");
}

static void load_reports_missing_file_with_display_path(void)
{
    char root[256];
    config_t cfg;
    config_error_t err;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_NOT_FOUND);
    CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
    CHECK_STR_NOT_CONTAINS(err.message, "build/tmp");
}

static void load_reads_the_config_from_the_sd_config_directory(void)
{
    char root[256];
    char *json = test_read_file_in(CONTRACTS_DIR, "switch-config.example.json");
    config_t cfg;
    config_error_t err;
    CHECK(json != NULL);
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "config/totk-sync/config.json", json, strlen(json)));
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_OK);
    CHECK_STR_EQ(cfg.pc_host, "192.168.1.10");
    CHECK_INT_EQ(cfg.pc_port, 8765);
    CHECK_STR_EQ(cfg.source_display_path, DISPLAY_PATH);
    free(json);
}

static void load_does_not_fall_back_to_the_phase0_jksv_config(void)
{
    char root[256];
    const char *legacy = "{\"origin\":\"x\",\"username\":\"u\",\"password\":\"p\"}";
    config_t cfg;
    config_error_t err;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "config/JKSV/webdav.json", legacy, strlen(legacy)));
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_NOT_FOUND);
}

static void load_reports_parse_errors_with_the_display_path(void)
{
    char root[256];
    const char *broken = "{ nope";
    config_t cfg;
    config_error_t err;
    CHECK(test_fs_make_root(root, sizeof root));
    CHECK(test_fs_write_file(root, "config/totk-sync/config.json", broken, strlen(broken)));
    CHECK_INT_EQ(config_load(root, &cfg, &err), CONFIG_ERR_PARSE);
    CHECK(strstr(err.message, DISPLAY_PATH) != NULL);
}

void run_config_tests(void)
{
    RUN_TEST(accepts_the_contract_example_file_as_is);
    RUN_TEST(accepts_the_contract_example_with_crlf_line_endings);
    RUN_TEST(accepts_the_contract_example_with_utf8_bom);
    RUN_TEST(reports_each_missing_required_field_by_name_and_path);
    RUN_TEST(reports_empty_and_blank_text_fields_by_name_and_path);
    RUN_TEST(rejects_a_token_with_a_space_or_control_or_non_ascii_character);
    RUN_TEST(error_for_a_bad_token_never_contains_the_token);
    RUN_TEST(token_length_limit_is_127_characters);
    RUN_TEST(port_boundaries_are_1024_to_65535);
    RUN_TEST(rejects_ports_that_are_not_whole_numbers);
    RUN_TEST(keep_backups_defaults_to_20_when_absent);
    RUN_TEST(keep_backups_accepts_one_and_larger_whole_numbers);
    RUN_TEST(keep_backups_rejects_zero_negative_fractional_and_non_numbers);
    RUN_TEST(rejects_public_loopback_link_local_and_named_hosts);
    RUN_TEST(accepts_private_hosts_including_the_longest_one);
    RUN_TEST(phase0_yandex_config_gets_the_exact_legacy_message);
    RUN_TEST(a_single_phase0_field_is_enough_to_report_legacy);
    RUN_TEST(legacy_message_does_not_leak_phase0_password);
    RUN_TEST(new_fields_next_to_leftover_phase0_fields_are_a_normal_config);
    RUN_TEST(an_empty_object_is_a_missing_field_not_legacy);
    RUN_TEST(broken_json_reports_parse_error_with_path_and_position);
    RUN_TEST(empty_or_non_object_json_is_rejected_without_crashing);
    RUN_TEST(successful_parse_clears_the_error);
    RUN_TEST(load_reports_missing_file_with_display_path);
    RUN_TEST(load_reads_the_config_from_the_sd_config_directory);
    RUN_TEST(load_does_not_fall_back_to_the_phase0_jksv_config);
    RUN_TEST(load_reports_parse_errors_with_the_display_path);
}
