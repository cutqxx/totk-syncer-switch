#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *test_read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (text != NULL) {
        size_t read = fread(text, 1, (size_t)size, file);
        text[read] = '\0';
    }
    fclose(file);
    return text;
}

char *test_read_file_in(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return test_read_file(path);
}

void test_fill_response(http_response_t *resp, const scripted_reply_t *reply)
{
    resp->http_code = reply->http_code;
    resp->curl_code = reply->curl_code;
    if (reply->headers != NULL) {
        snprintf(resp->response_headers, sizeof resp->response_headers, "%s", reply->headers);
    }
    if (reply->body != NULL) {
        body_append(&resp->body, reply->body, strlen(reply->body), TS_MAX_JSON_BYTES);
    }
}

static void fake_perform(void *ctx, const http_request_t *req, http_response_t *out)
{
    fake_world_t *world = ctx;
    int index = world->net_calls < world->reply_count ? world->net_calls : world->reply_count - 1;
    world->last_request = *req;
    world->net_calls++;
    if (req->body != NULL) {
        bool is_other_body = world->sent_body.data != NULL
                             && (world->sent_body.size != req->body->size
                                 || memcmp(world->sent_body.data, req->body->data, (size_t)req->body->size) != 0);
        world->is_sent_body_changed = world->is_sent_body_changed || is_other_body;
        body_free(&world->sent_body);
        body_append(&world->sent_body, req->body->data, (size_t)req->body->size, INT64_MAX);
    }
    test_fill_response(out, &world->replies[index]);
    out->bytes_sent = world->reply_bytes_sent;
    out->seconds = world->reply_seconds;
}

static void fake_now_local(void *ctx, struct tm *out)
{
    *out = ((fake_world_t *)ctx)->fixed_time;
}

static void fake_sleep_seconds(void *ctx, int seconds)
{
    fake_world_t *world = ctx;
    if (world->sleep_count < MAX_RECORDED_LINES) {
        world->slept_seconds[world->sleep_count] = seconds;
    }
    world->sleep_count++;
}

static void fake_show_line(void *ctx, const char *line)
{
    fake_world_t *world = ctx;
    if (world->ui_line_count < MAX_RECORDED_LINES) {
        snprintf(world->ui_lines[world->ui_line_count], TS_LINE_SIZE, "%s", line);
    }
    world->ui_line_count++;
}

static int fake_choose_account(void *ctx, const account_t *list, int count)
{
    fake_world_t *world = ctx;
    (void)list;
    world->choose_calls++;
    world->choose_offered = count;
    return world->choose_result;
}

void fake_world_init(fake_world_t *world, const scripted_reply_t *replies, int reply_count)
{
    memset(world, 0, sizeof *world);
    world->replies = replies;
    world->reply_count = reply_count;
    world->fixed_time.tm_year = 126;
    world->fixed_time.tm_mon = 10;
    world->fixed_time.tm_mday = 7;
    world->fixed_time.tm_hour = 12;
    world->net = (net_port_t){ .ctx = world, .perform = fake_perform };
    world->clock = (clock_port_t){ .ctx = world, .now_local = fake_now_local, .sleep_seconds = fake_sleep_seconds };
    world->ui = (ui_port_t){ .ctx = world, .show_line = fake_show_line, .choose_account = fake_choose_account };
    world->choose_result = -1;
    world->env = (sync_env_t){ &world->net, &world->clock, &world->ui, NULL, NULL };
}

void test_make_config(config_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    snprintf(cfg->pc_host, sizeof cfg->pc_host, "192.168.1.10");
    cfg->pc_port = 8765;
    snprintf(cfg->token, sizeof cfg->token, "%s", CONTRACT_TOKEN);
    cfg->keep_backups = 20;
    snprintf(cfg->source_display_path, sizeof cfg->source_display_path, "sd:/config/totk-sync/config.json");
}

bool test_is_ascii_text(const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        if (*p != '\n' && (*p < 0x20 || *p > 0x7E)) {
            return false;
        }
    }
    return true;
}

void fake_world_release(fake_world_t *world)
{
    body_free(&world->sent_body);
}

static void fake_find_game_process(void *ctx, game_process_info_t *out)
{
    fake_console_t *console = ctx;
    console->probe_calls++;
    if (!console->is_probe_left_untouched) {
        *out = console->probe;
    }
}

static int fake_list_accounts(void *ctx, account_t *out, int max)
{
    fake_console_t *console = ctx;
    int count = console->account_count < max ? console->account_count : max;
    console->list_calls++;
    for (int i = 0; i < count; i++) {
        out[i] = console->accounts[i];
    }
    return count;
}

static bool fake_mount(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code)
{
    fake_console_t *console = ctx;
    console->mount_calls++;
    console->mounted_account = *account;
    console->net_calls_at_mount = console->world->net_calls;
    if (!console->does_mount_succeed) {
        *error_code = console->mount_error;
        return false;
    }
    snprintf(root_out, root_size, "%s", console->mount_root);
    return true;
}

static void fake_unmount(void *ctx)
{
    fake_console_t *console = ctx;
    console->unmount_calls++;
    console->net_calls_at_unmount = console->world->net_calls;
}

void fake_console_init(fake_console_t *console, fake_world_t *world)
{
    static const uint64_t uid[2] = { 0x1111, 0x2222 };
    memset(console, 0, sizeof *console);
    console->world = world;
    console->probe.is_probe_ok = true;
    console->probe.self_pid = 77;
    console->account_count = 1;
    account_make(&console->accounts[0], uid, "Player", 1);
    console->does_mount_succeed = true;
    console->net_calls_at_mount = -1;
    console->net_calls_at_unmount = -1;
    console->port = (console_port_t){ .ctx = console, .find_game_process = fake_find_game_process,
                                      .list_accounts_with_save = fake_list_accounts, .mount_save_readonly = fake_mount,
                                      .unmount_save = fake_unmount };
    world->env.console = &console->port;
}
