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
    test_fill_response(out, &world->replies[index]);
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
    world->ui = (ui_port_t){ .ctx = world, .show_line = fake_show_line };
    world->env = (sync_env_t){ &world->net, &world->clock, &world->ui, NULL };
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
