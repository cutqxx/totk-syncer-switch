#include <string.h>

#include "experiment.h"
#include "test.h"

static void tag_of_origin(const char *origin, char *tag, size_t size)
{
    experiment_t e = experiment_default(origin);
    experiment_tag(&e, tag, size);
}

static void default_for_yandex_com_origin_tags_com_with_spec_defaults(void)
{
    char tag[48];
    tag_of_origin("https://webdav.yandex.com", tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|noexpect|cl|t120");
}

static void default_for_yandex_ru_origin_tags_ru(void)
{
    char tag[48];
    tag_of_origin("https://webdav.yandex.ru", tag, sizeof tag);
    CHECK_STR_EQ(tag, "ru|noexpect|cl|t120");
}

static void default_for_foreign_origin_tags_other(void)
{
    char tag[48];
    tag_of_origin("https://dav.example.com/remote.php", tag, sizeof tag);
    CHECK_STR_EQ(tag, "other|noexpect|cl|t120");
}

static void origin_with_trailing_slash_is_still_recognised(void)
{
    char tag[48];
    tag_of_origin("https://webdav.yandex.ru/", tag, sizeof tag);
    CHECK_STR_EQ(tag, "ru|noexpect|cl|t120");
}

static void origin_that_merely_starts_with_yandex_host_is_other(void)
{
    char tag[48];
    tag_of_origin("https://webdav.yandex.com.evil.test", tag, sizeof tag);
    CHECK_STR_EQ(tag, "other|noexpect|cl|t120");
}

static void empty_origin_is_other(void)
{
    char tag[48];
    tag_of_origin("", tag, sizeof tag);
    CHECK_STR_EQ(tag, "other|noexpect|cl|t120");
}

static void tag_reflects_every_switched_flag(void)
{
    char tag[48];
    experiment_t e = experiment_default("https://webdav.yandex.com");
    e.is_expect_100 = true;
    e.is_chunked = true;
    e.is_long_timeouts = true;
    experiment_tag(&e, tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|expect|chunked|t600");
}

static void tag_is_truncated_safely_into_small_buffer(void)
{
    char tag[8];
    tag_of_origin("https://webdav.yandex.com", tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|noe");
}

#define COM_ORIGIN "https://webdav.yandex.com"

static void toggled_tag(exp_param_t param, int times, char *tag, size_t size)
{
    experiment_t e = experiment_default(COM_ORIGIN);
    for (int i = 0; i < times; i++) {
        experiment_toggle(&e, param);
    }
    experiment_tag(&e, tag, size);
}

static void toggling_server_switches_com_to_ru_in_tag(void)
{
    char tag[48];
    toggled_tag(EXP_PARAM_SERVER, 1, tag, sizeof tag);
    CHECK_STR_EQ(tag, "ru|noexpect|cl|t120");
}

static void toggling_expect_turns_it_on_in_tag(void)
{
    char tag[48];
    toggled_tag(EXP_PARAM_EXPECT, 1, tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|expect|cl|t120");
}

static void toggling_body_switches_to_chunked_in_tag(void)
{
    char tag[48];
    toggled_tag(EXP_PARAM_BODY, 1, tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|noexpect|chunked|t120");
}

static void toggling_timeouts_switches_to_t600_in_tag(void)
{
    char tag[48];
    toggled_tag(EXP_PARAM_TIMEOUTS, 1, tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|noexpect|cl|t600");
}

static void toggling_each_parameter_twice_restores_default_tag(void)
{
    char tag[48];
    for (int param = EXP_PARAM_SERVER; param <= EXP_PARAM_TIMEOUTS; param++) {
        toggled_tag((exp_param_t)param, 2, tag, sizeof tag);
        CHECK_STR_EQ(tag, "com|noexpect|cl|t120");
    }
}

static void toggling_one_parameter_leaves_the_others_alone(void)
{
    char tag[48];
    experiment_t e = experiment_default(COM_ORIGIN);
    experiment_toggle(&e, EXP_PARAM_EXPECT);
    experiment_toggle(&e, EXP_PARAM_TIMEOUTS);
    experiment_toggle(&e, EXP_PARAM_EXPECT);
    experiment_tag(&e, tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|noexpect|cl|t600");
}

static void toggling_server_from_ru_returns_to_com(void)
{
    char tag[48];
    experiment_t e = experiment_default("https://webdav.yandex.ru");
    experiment_toggle(&e, EXP_PARAM_SERVER);
    experiment_tag(&e, tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|noexpect|cl|t120");
}

static void toggling_server_from_other_goes_to_com(void)
{
    char tag[48];
    experiment_t e = experiment_default("https://dav.example.com");
    experiment_toggle(&e, EXP_PARAM_SERVER);
    experiment_tag(&e, tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|noexpect|cl|t120");
}

static void default_timeouts_are_15_and_120_seconds(void)
{
    experiment_t e = experiment_default(COM_ORIGIN);
    CHECK_INT_EQ(experiment_connect_timeout_s(&e), 15);
    CHECK_INT_EQ(experiment_total_timeout_s(&e), 120);
}

static void long_timeouts_are_30_and_600_seconds(void)
{
    experiment_t e = experiment_default(COM_ORIGIN);
    experiment_toggle(&e, EXP_PARAM_TIMEOUTS);
    CHECK_INT_EQ(experiment_connect_timeout_s(&e), 30);
    CHECK_INT_EQ(experiment_total_timeout_s(&e), 600);
}

static void origin_follows_server_variant_for_yandex_configs(void)
{
    char out[64];
    experiment_t e = experiment_default(COM_ORIGIN);
    CHECK(experiment_origin(&e, COM_ORIGIN, out, sizeof out));
    CHECK_STR_EQ(out, "https://webdav.yandex.com");
    experiment_toggle(&e, EXP_PARAM_SERVER);
    CHECK(experiment_origin(&e, COM_ORIGIN, out, sizeof out));
    CHECK_STR_EQ(out, "https://webdav.yandex.ru");
}

static void origin_for_other_variant_is_config_origin_unchanged(void)
{
    char out[64];
    experiment_t e = experiment_default("https://dav.example.com/x");
    CHECK(experiment_origin(&e, "https://dav.example.com/x", out, sizeof out));
    CHECK_STR_EQ(out, "https://dav.example.com/x");
}

static void origin_reports_failure_when_buffer_is_too_small(void)
{
    char out[8];
    experiment_t e = experiment_default(COM_ORIGIN);
    CHECK(!experiment_origin(&e, COM_ORIGIN, out, sizeof out));
}

static void default_ignores_previous_toggles_and_gives_spec_values(void)
{
    char tag[48];
    experiment_t used = experiment_default(COM_ORIGIN);
    experiment_toggle(&used, EXP_PARAM_SERVER);
    experiment_toggle(&used, EXP_PARAM_EXPECT);
    experiment_toggle(&used, EXP_PARAM_BODY);
    experiment_toggle(&used, EXP_PARAM_TIMEOUTS);
    experiment_t fresh = experiment_default(COM_ORIGIN);
    experiment_tag(&fresh, tag, sizeof tag);
    CHECK_STR_EQ(tag, "com|noexpect|cl|t120");
}

void run_experiment_tests(void)
{
    RUN_TEST(toggling_server_switches_com_to_ru_in_tag);
    RUN_TEST(toggling_expect_turns_it_on_in_tag);
    RUN_TEST(toggling_body_switches_to_chunked_in_tag);
    RUN_TEST(toggling_timeouts_switches_to_t600_in_tag);
    RUN_TEST(toggling_each_parameter_twice_restores_default_tag);
    RUN_TEST(toggling_one_parameter_leaves_the_others_alone);
    RUN_TEST(toggling_server_from_ru_returns_to_com);
    RUN_TEST(toggling_server_from_other_goes_to_com);
    RUN_TEST(default_timeouts_are_15_and_120_seconds);
    RUN_TEST(long_timeouts_are_30_and_600_seconds);
    RUN_TEST(origin_follows_server_variant_for_yandex_configs);
    RUN_TEST(origin_for_other_variant_is_config_origin_unchanged);
    RUN_TEST(origin_reports_failure_when_buffer_is_too_small);
    RUN_TEST(default_ignores_previous_toggles_and_gives_spec_values);
    RUN_TEST(default_for_yandex_com_origin_tags_com_with_spec_defaults);
    RUN_TEST(default_for_yandex_ru_origin_tags_ru);
    RUN_TEST(default_for_foreign_origin_tags_other);
    RUN_TEST(origin_with_trailing_slash_is_still_recognised);
    RUN_TEST(origin_that_merely_starts_with_yandex_host_is_other);
    RUN_TEST(empty_origin_is_other);
    RUN_TEST(tag_reflects_every_switched_flag);
    RUN_TEST(tag_is_truncated_safely_into_small_buffer);
}
