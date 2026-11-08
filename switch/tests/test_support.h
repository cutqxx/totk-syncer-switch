#ifndef TS_TEST_SUPPORT_H
#define TS_TEST_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "ports.h"

#define CONTRACTS_DIR "../../contracts"
#define SOURCE_DIR "../source"
#define CONTRACT_TOKEN "0000000000000000000000000000000000000000000000000000000000000000"
#define LAUNCHER_MARKER_HEADERS "HTTP/1.1 200 OK\r\nX-TotkSync-Api: 1\r\nContent-Type: application/json\r\n\r\n"

/* Reads a whole file into a malloc'd NUL-terminated buffer; NULL when it cannot be read. Caller frees. */
char *test_read_file(const char *path);

/* Same, for <dir>/<name>. */
char *test_read_file_in(const char *dir, const char *name);

/* A scripted network answer. headers: raw response header lines; body: may be NULL. */
typedef struct {
    long http_code;
    int curl_code;
    const char *headers;
    const char *body;
} scripted_reply_t;

#define MAX_RECORDED_LINES 8

/* Fake net + clock + ui in one place. The last scripted reply repeats when the script runs out. */
typedef struct {
    const scripted_reply_t *replies;
    int reply_count;
    int net_calls;
    http_request_t last_request;
    int sleep_count;
    int slept_seconds[MAX_RECORDED_LINES];
    int64_t reply_bytes_sent; /* reported as bytes_sent by every scripted reply */
    double reply_seconds;     /* reported as seconds by every scripted reply */
    body_t sent_body;         /* copy of the body of the last request that carried one (the original may be freed) */
    bool is_sent_body_changed; /* a later request carried a different body than an earlier one */
    int choose_calls;
    int choose_offered;       /* account count shown to the last choose_account call */
    int choose_result;        /* what choose_account answers; -1 cancels */
    int ui_line_count;
    char ui_lines[MAX_RECORDED_LINES][TS_LINE_SIZE];
    struct tm fixed_time;
    net_port_t net;
    clock_port_t clock;
    ui_port_t ui;
    sync_env_t env;
} fake_world_t;

void fake_world_init(fake_world_t *world, const scripted_reply_t *replies, int reply_count);

/* Releases what the fake kept (the copied request body). */
void fake_world_release(fake_world_t *world);

/* Fake console: describes what the Switch "reports" and records every call. world->env.console points to it. */
typedef struct {
    game_process_info_t probe;
    bool is_probe_left_untouched; /* find_game_process writes nothing, like a broken port */
    account_t accounts[ACCOUNT_MAX];
    int account_count;
    bool does_mount_succeed;
    uint32_t mount_error;
    char mount_root[256];         /* what a successful mount reports as the root of the save */
    int probe_calls;
    int list_calls;
    int mount_calls;
    int unmount_calls;
    account_t mounted_account;    /* the account of the last mount call */
    int net_calls_at_mount;       /* world->net_calls when the last mount happened; -1 before any */
    int net_calls_at_unmount;     /* the same for unmount */
    fake_world_t *world;
    console_port_t port;
} fake_console_t;

/* One account "Player", game not running, mount succeeds with an empty root. */
void fake_console_init(fake_console_t *console, fake_world_t *world);

/* Fills a response the way the curl adapter would. */
void test_fill_response(http_response_t *resp, const scripted_reply_t *reply);

/* A valid config pointing at 192.168.1.10:8765 with the contract token. */
void test_make_config(config_t *cfg);


/* True when every byte is printable ASCII (0x20-0x7E) or a newline. */
bool test_is_ascii_text(const char *text);

#endif
