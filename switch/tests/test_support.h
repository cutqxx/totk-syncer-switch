#ifndef TS_TEST_SUPPORT_H
#define TS_TEST_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>

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
    int ui_line_count;
    char ui_lines[MAX_RECORDED_LINES][TS_LINE_SIZE];
    struct tm fixed_time;
    net_port_t net;
    clock_port_t clock;
    ui_port_t ui;
    sync_env_t env;
} fake_world_t;

void fake_world_init(fake_world_t *world, const scripted_reply_t *replies, int reply_count);

/* Fills a response the way the curl adapter would. */
void test_fill_response(http_response_t *resp, const scripted_reply_t *reply);

/* A valid config pointing at 192.168.1.10:8765 with the contract token. */
void test_make_config(config_t *cfg);


/* True when every byte is printable ASCII (0x20-0x7E) or a newline. */
bool test_is_ascii_text(const char *text);

#endif
