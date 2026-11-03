#include "ui_console.h"

#include <stdio.h>
#include <switch.h>

#include "text_ru.h"

#define ANSI_RED_BOLD CONSOLE_RED
#define ANSI_RESET CONSOLE_RESET

void ui_init(void)
{
    consoleInit(NULL);
}

void ui_exit(void)
{
    consoleExit(NULL);
}

static const char *source_label(config_source_t source)
{
    return source == CONFIG_SOURCE_JKSV ? TXT_CONFIG_SOURCE_JKSV : TXT_CONFIG_SOURCE_TOTK_SYNC;
}

static const char *server_label(exp_server_t server, const char *config_origin)
{
    switch (server) {
    case EXP_SERVER_COM: return "webdav.yandex.com";
    case EXP_SERVER_RU: return "webdav.yandex.ru";
    default: return config_origin;
    }
}

static void draw_experiment(const config_t *cfg, const experiment_t *experiment)
{
    printf(TXT_LABEL_EXP_SERVER "\n", server_label(experiment->server, cfg->origin));
    printf(TXT_LABEL_EXP_EXPECT "\n", experiment->is_expect_100 ? TXT_EXP_ON : TXT_EXP_OFF);
    printf(TXT_LABEL_EXP_BODY "\n", experiment->is_chunked ? TXT_EXP_BODY_CHUNKED : TXT_EXP_BODY_LENGTH);
    printf(TXT_LABEL_EXP_TIMEOUTS "\n\n", experiment->is_long_timeouts ? TXT_EXP_TIMEOUTS_LONG : TXT_EXP_TIMEOUTS_NORMAL);
    printf("%s\n", TXT_BUTTONS_EXPERIMENT);
}

static void draw_config_summary(const config_t *cfg, const experiment_t *experiment, const char *destination_url)
{
    printf(TXT_LABEL_CONFIG "\n", cfg->source_display_path, source_label(cfg->source));
    printf(TXT_LABEL_ORIGIN "\n", cfg->origin);
    printf(TXT_LABEL_USERNAME "\n", cfg->username);
    printf(TXT_LABEL_DESTINATION "\n\n", destination_url);
    draw_experiment(cfg, experiment);
    printf("%s\n", TXT_BUTTONS_READY);
}

static void draw_init_error(const char *init_error)
{
    if (init_error[0] != '\0') {
        printf(ANSI_RED_BOLD "%s" ANSI_RESET "\n\n", init_error);
    }
}

void ui_draw_menu(const config_t *cfg, const experiment_t *experiment, const char *config_error, const char *destination_url, const char *init_error)
{
    consoleClear();
    printf("%s\n%s\n\n", TXT_TITLE, TXT_FONT_PROBE);
    draw_init_error(init_error);
    if (cfg != NULL) {
        draw_config_summary(cfg, experiment, destination_url);
    } else {
        printf("%s\n\n%s\n", config_error, TXT_BUTTONS_BLOCKED);
    }
    printf("\n");
    consoleUpdate(NULL);
}

void ui_show_line(void *ctx, const char *line)
{
    (void)ctx;
    printf("%s\n", line);
    consoleUpdate(NULL);
}
