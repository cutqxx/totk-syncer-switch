#include <stdio.h>
#include <switch.h>

#include "clock_nx.h"
#include "config.h"
#include "experiment.h"
#include "http_curl.h"
#include "log.h"
#include "ts_limits.h"
#include "session.h"
#include "text_ru.h"
#include "ui_console.h"
#include "url.h"

#define SD_ROOT "sdmc:"

static upload_port_t make_port(void)
{
    upload_port_t port = {
        .ctx = NULL,
        .perform = http_curl_perform,
        .sleep_seconds = clock_sleep_seconds,
        .now_local = clock_now_local,
        .show_line = ui_show_line,
    };
    return port;
}

static void preview_destination(const config_t *cfg, const experiment_t *experiment, char *out, size_t out_size)
{
    struct tm now;
    char remote_name[64];
    char origin[TS_URL_SIZE];
    clock_now_local(NULL, &now);
    url_format_remote_name(&now, remote_name, sizeof remote_name);
    experiment_origin(experiment, cfg->origin, origin, sizeof origin);
    url_build_destination(origin, cfg->basepath, remote_name, out, out_size);
}

static void draw_menu(const config_t *cfg, const experiment_t *experiment, const char *config_error, const char *init_error)
{
    char destination[TS_URL_SIZE] = "";
    if (cfg != NULL) {
        preview_destination(cfg, experiment, destination, sizeof destination);
    }
    ui_draw_menu(cfg, experiment, config_error, destination, init_error);
}

static bool is_toggle_pressed(u64 pressed, u64 button, exp_param_t param, experiment_t *experiment)
{
    if (!(pressed & button)) {
        return false;
    }
    experiment_toggle(experiment, param);
    return true;
}

static bool apply_toggles(u64 pressed, experiment_t *experiment)
{
    bool is_server = is_toggle_pressed(pressed, HidNpadButton_X, EXP_PARAM_SERVER, experiment);
    bool is_expect = is_toggle_pressed(pressed, HidNpadButton_Y, EXP_PARAM_EXPECT, experiment);
    bool is_body = is_toggle_pressed(pressed, HidNpadButton_L, EXP_PARAM_BODY, experiment);
    bool is_timeouts = is_toggle_pressed(pressed, HidNpadButton_R, EXP_PARAM_TIMEOUTS, experiment);
    return is_server || is_expect || is_body || is_timeouts;
}

static void run_main_loop(PadState *pad, const config_t *cfg, bool is_config_loaded, const char *config_error, const char *init_error)
{
    upload_port_t port = make_port();
    experiment_t experiment = experiment_default(is_config_loaded ? cfg->origin : "");
    log_sink_t log;
    log_sink_init(&log, SD_ROOT);
    session_inputs_t inputs = { cfg, SD_ROOT, &experiment, &log };
    const config_t *menu_config = is_config_loaded ? cfg : NULL;
    draw_menu(menu_config, &experiment, config_error, init_error);
    while (appletMainLoop()) {
        padUpdate(pad);
        u64 pressed = padGetButtonsDown(pad);
        if (pressed & HidNpadButton_Plus) {
            break;
        }
        if (is_config_loaded && apply_toggles(pressed, &experiment)) {
            draw_menu(menu_config, &experiment, config_error, init_error);
        }
        if ((pressed & HidNpadButton_A) && is_config_loaded) {
            session_upload_test_file(&inputs, &port);
        }
        consoleUpdate(NULL);
    }
}

static bool init_network(char *error, size_t error_size)
{
    Result socket_result = socketInitializeDefault();
    if (R_FAILED(socket_result)) {
        snprintf(error, error_size, TXT_SOCKET_INIT_FAILED, (unsigned)socket_result);
        return false;
    }
    if (!http_curl_global_init()) {
        snprintf(error, error_size, "%s", TXT_CURL_INIT_FAILED);
        socketExit();
        return false;
    }
    return true;
}

int main(void)
{
    PadState pad;
    config_t cfg;
    config_error_t error;
    char init_error[96] = "";

    ui_init();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    bool is_network_ready = init_network(init_error, sizeof init_error);

    bool is_config_loaded = config_load(SD_ROOT, &cfg, &error) == CONFIG_OK;
    run_main_loop(&pad, &cfg, is_config_loaded, error.message, init_error);

    if (is_network_ready) {
        http_curl_global_cleanup();
        socketExit();
    }
    ui_exit();
    return 0;
}
