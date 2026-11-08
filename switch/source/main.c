#include <stdio.h>
#include <switch.h>

#include "clock_nx.h"
#include "config.h"
#include "http_curl.h"
#include "log.h"
#include "main_screen.h"
#include "pc_status.h"
#include "ports.h"
#include "save_fs.h"
#include "send_to_pc.h"
#include "system_nx.h"
#include "text.h"
#include "ui_console.h"

#define SD_ROOT "sdmc:"

static void draw(const config_t *cfg, const char *blocking_error, const pc_status_t *status)
{
    char text[MAIN_SCREEN_SIZE];
    main_screen_render(cfg, blocking_error, status, text, sizeof text);
    ui_draw_screen(text);
}

static void refresh_status(const sync_env_t *env, const config_t *cfg, pc_status_t *status)
{
    draw(cfg, NULL, NULL);
    pc_status_refresh(env, cfg, status);
    draw(cfg, NULL, status);
}

static void send_and_show_result(const sync_env_t *env, const config_t *cfg, pc_status_t *status)
{
    send_result_t result;
    char text[MAIN_SCREEN_SIZE];
    ui_draw_screen(TXT_SEND_TITLE "\n");
    send_to_pc(env, cfg, TS_MAX_SAVE_BYTES, &result);
    if (result.kind == SEND_OK) {
        pc_status_refresh(env, cfg, status);
    }
    main_screen_render_with_result(cfg, status, result.message, text, sizeof text);
    ui_draw_screen(text);
}

static void wait_for_exit(PadState *pad)
{
    while (appletMainLoop()) {
        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(NULL);
    }
}

static void run_connected_loop(PadState *pad, const config_t *cfg)
{
    const net_port_t net = { .ctx = NULL, .perform = http_curl_perform };
    const clock_port_t clock = { .ctx = NULL, .now_local = clock_now_local, .sleep_seconds = clock_sleep_seconds };
    const ui_port_t ui = { .ctx = pad, .show_line = ui_show_line, .choose_account = ui_choose_account };
    const console_port_t console = { .ctx = NULL,
                                     .find_game_process = system_nx_find_game_process,
                                     .list_accounts_with_save = system_nx_list_accounts_with_save,
                                     .mount_save_readonly = save_fs_mount_readonly,
                                     .unmount_save = save_fs_unmount };
    log_sink_t log;
    log_sink_init(&log, SD_ROOT, cfg->token);
    const sync_env_t env = { &net, &clock, &ui, &log, &console };
    pc_status_t status;

    system_nx_init();
    refresh_status(&env, cfg, &status);
    while (appletMainLoop()) {
        padUpdate(pad);
        u64 pressed = padGetButtonsDown(pad);
        if (pressed & HidNpadButton_Plus) {
            break;
        }
        if (pressed & HidNpadButton_R) {
            refresh_status(&env, cfg, &status);
        }
        if (pressed & HidNpadButton_A) {
            send_and_show_result(&env, cfg, &status);
        }
        consoleUpdate(NULL);
    }
    system_nx_exit();
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

static void run_blocked(PadState *pad, const char *blocking_error)
{
    draw(NULL, blocking_error, NULL);
    wait_for_exit(pad);
}

int main(void)
{
    PadState pad;
    config_t cfg;
    config_error_t config_error;
    char init_error[96] = "";

    ui_init();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    bool is_network_ready = init_network(init_error, sizeof init_error);
    bool is_config_loaded = config_load(SD_ROOT, &cfg, &config_error) == CONFIG_OK;

    if (!is_config_loaded) {
        run_blocked(&pad, config_error.message);
    } else if (!is_network_ready) {
        run_blocked(&pad, init_error);
    } else {
        run_connected_loop(&pad, &cfg);
    }

    if (is_network_ready) {
        http_curl_global_cleanup();
        socketExit();
    }
    ui_exit();
    return 0;
}
