#include "test_support.h"

#include <errno.h>
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
    if (world->net_calls < MAX_RECORDED_LINES) {
        snprintf(world->request_urls[world->net_calls], TS_URL_SIZE, "%s", req->url);
    }
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
    if (world->binary_body != NULL && index == world->binary_reply_index) {
        body_free(&out->body);
        body_append(&out->body, world->binary_body, world->binary_body_size, INT64_MAX);
    }
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
    snprintf(world->last_ui_line, sizeof world->last_ui_line, "%s", line);
    world->ui_line_count++;
}

static int fake_choose_account(void *ctx, const char *title, const account_t *list, int count)
{
    fake_world_t *world = ctx;
    (void)list;
    snprintf(world->choose_title, sizeof world->choose_title, "%s", title);
    world->choose_calls++;
    world->choose_offered = count;
    return world->choose_result;
}

static bool fake_confirm(void *ctx, const char *prompt)
{
    fake_world_t *world = ctx;
    int index = world->confirm_calls++;
    snprintf(world->confirm_prompt, sizeof world->confirm_prompt, "%s", prompt);
    if (index < MAX_RECORDED_PROMPTS) {
        snprintf(world->confirm_prompts[index], TS_LINE_SIZE, "%s", prompt);
    }
    return index < world->confirm_answer_count ? world->confirm_answers[index] : world->confirm_answer;
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
    world->ui = (ui_port_t){ .ctx = world, .show_line = fake_show_line, .choose_account = fake_choose_account,
                              .confirm = fake_confirm };
    world->choose_result = -1;
    world->env = (sync_env_t){ &world->net, &world->clock, &world->ui, NULL, NULL, file_ops_stdio(), NULL };
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
    int index = console->probe_calls++;
    if (!console->is_probe_left_untouched) {
        *out = index < console->probe_results_count ? console->probe_results[index] : console->probe;
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
    console->is_mounted = true;
    return true;
}

static void fake_unmount(void *ctx)
{
    fake_console_t *console = ctx;
    console->unmount_calls++;
    console->is_mounted = false;
    console->net_calls_at_unmount = console->world->net_calls;
}

static bool fake_mount_rw(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code)
{
    fake_console_t *console = ctx;
    console->rw_mount_calls++;
    console->mounted_account = *account;
    if (!console->does_rw_mount_succeed || console->rw_mount_calls == console->fail_rw_mount_at) {
        *error_code = console->rw_mount_error;
        return false;
    }
    snprintf(root_out, root_size, "%s", console->mount_root);
    console->is_mounted = true;
    return true;
}

static bool fake_commit(void *ctx, uint32_t *error_code)
{
    fake_console_t *console = ctx;
    console->commit_calls++;
    if (console->watched_files != NULL) {
        console->commits_with_open_files += console->watched_files->open_handles > 0 ? 1 : 0;
    }
    if (console->commit_calls == console->fail_commit_at) {
        *error_code = console->commit_error;
        return false;
    }
    if (console->watched_files != NULL) {
        console->watched_files->bytes_since_commit = 0;
    }
    return true;
}

static bool fake_read_space(void *ctx, const account_t *account, int64_t *data_bytes, int64_t *journal_bytes)
{
    fake_console_t *console = ctx;
    (void)account;
    *data_bytes = console->data_bytes;
    *journal_bytes = console->journal_bytes;
    return console->is_space_known;
}

static void fake_set_home_blocked(void *ctx, bool is_blocked)
{
    fake_console_t *console = ctx;
    console->is_home_blocked = is_blocked;
    if (is_blocked) {
        console->home_block_calls++;
    } else {
        console->home_unblock_calls++;
    }
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
    console->does_rw_mount_succeed = true;
    console->port = (console_port_t){ .ctx = console, .find_game_process = fake_find_game_process,
                                      .list_accounts_with_save = fake_list_accounts, .mount_save_readonly = fake_mount,
                                      .unmount_save = fake_unmount, .mount_save_readwrite = fake_mount_rw,
                                      .commit_save = fake_commit, .read_save_space = fake_read_space,
                                      .set_home_blocked = fake_set_home_blocked };
    world->env.console = &console->port;
}

static bool faulty_counts(faulty_files_t *files, fault_step_t step, const char *path)
{
    if (files->only_under != NULL && (path == NULL || strncmp(path, files->only_under, strlen(files->only_under)) != 0)) {
        return false;
    }
    files->calls[step]++;
    if (step == files->fail_step && files->calls[step] == files->fail_at) {
        errno = ENOSPC;
        return true;
    }
    return false;
}

typedef struct {
    void *file;
    char path[512];
} faulty_handle_t;

static void *faulty_open(void *ctx, const char *path, bool is_append)
{
    faulty_files_t *files = ctx;
    if (faulty_counts(files, FAULT_OPEN, path)) {
        return NULL;
    }
    faulty_handle_t *handle = calloc(1, sizeof *handle);
    handle->file = file_ops_stdio()->open_write(NULL, path, is_append);
    if (handle->file == NULL) {
        free(handle);
        return NULL;
    }
    snprintf(handle->path, sizeof handle->path, "%s", path);
    files->open_handles++;
    files->mutations++;
    return handle;
}

static bool faulty_write(void *ctx, void *file, const void *data, size_t size)
{
    faulty_files_t *files = ctx;
    faulty_handle_t *handle = file;
    if (faulty_counts(files, FAULT_WRITE, handle->path)) {
        size_t half = size / 2;
        file_ops_stdio()->write(NULL, handle->file, data, half);
        errno = ENOSPC;
        return false;
    }
    bool is_written = file_ops_stdio()->write(NULL, handle->file, data, size);
    files->mutations += is_written ? 1 : 0;
    files->bytes_since_commit += is_written ? (int64_t)size : 0;
    if (files->bytes_since_commit > files->max_bytes_between_commits) {
        files->max_bytes_between_commits = files->bytes_since_commit;
    }
    return is_written;
}

static bool faulty_close(void *ctx, void *file)
{
    faulty_files_t *files = ctx;
    faulty_handle_t *handle = file;
    bool is_failing = faulty_counts(files, FAULT_CLOSE, handle->path);
    bool is_closed = file_ops_stdio()->close(NULL, handle->file);
    files->open_handles--;
    free(handle);
    if (is_failing) {
        errno = ENOSPC;
        return false;
    }
    return is_closed;
}

static bool faulty_path_op(faulty_files_t *files, fault_step_t step, const char *path, bool (*real)(void *, const char *))
{
    if (faulty_counts(files, step, path)) {
        return false;
    }
    bool is_done = real(NULL, path);
    files->mutations += is_done ? 1 : 0;
    return is_done;
}

static bool faulty_make_dir(void *ctx, const char *path)
{
    return faulty_path_op(ctx, FAULT_MAKE_DIR, path, file_ops_stdio()->make_dir);
}

static bool faulty_remove_file(void *ctx, const char *path)
{
    return faulty_path_op(ctx, FAULT_REMOVE_FILE, path, file_ops_stdio()->remove_file);
}

static bool faulty_remove_dir(void *ctx, const char *path)
{
    return faulty_path_op(ctx, FAULT_REMOVE_DIR, path, file_ops_stdio()->remove_dir);
}

static bool faulty_rename(void *ctx, const char *from, const char *to)
{
    faulty_files_t *files = ctx;
    if (faulty_counts(files, FAULT_RENAME, from)) {
        return false;
    }
    bool is_done = file_ops_stdio()->rename(NULL, from, to);
    files->mutations += is_done ? 1 : 0;
    return is_done;
}

void faulty_files_init(faulty_files_t *files)
{
    memset(files, 0, sizeof *files);
    files->ops = (file_ops_t){ .ctx = files, .open_write = faulty_open, .write = faulty_write, .close = faulty_close,
                               .make_dir = faulty_make_dir, .remove_file = faulty_remove_file,
                               .remove_dir = faulty_remove_dir, .rename = faulty_rename };
}
