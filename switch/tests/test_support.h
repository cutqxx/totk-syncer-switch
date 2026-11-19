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
#define MAX_SCRIPTED_ANSWERS 4
#define MAX_RECORDED_PROMPTS 4

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
    const unsigned char *binary_body; /* when set: the body of the reply with index binary_reply_index (a ZIP) */
    size_t binary_body_size;
    int binary_reply_index;
    body_t sent_body;         /* copy of the body of the last request that carried one (the original may be freed) */
    bool is_sent_body_changed; /* a later request carried a different body than an earlier one */
    int choose_calls;
    int choose_offered;       /* account count shown to the last choose_account call */
    int choose_result;        /* what choose_account answers; -1 cancels */
    char choose_title[TS_LINE_SIZE]; /* the title of the last choose_account call */
    int confirm_calls;
    bool confirm_answer;      /* what confirm answers: true is A, false is B */
    bool confirm_answers[MAX_SCRIPTED_ANSWERS]; /* when confirm_answer_count > 0: the answers in order, then confirm_answer */
    int confirm_answer_count;
    char confirm_prompt[TS_LINE_SIZE]; /* the prompt of the last confirm call */
    char confirm_prompts[MAX_RECORDED_PROMPTS][TS_LINE_SIZE]; /* every prompt, in order */
    char request_urls[MAX_RECORDED_LINES][TS_URL_SIZE]; /* the URL of every request, in order */
    int ui_line_count;
    char ui_lines[MAX_RECORDED_LINES][TS_LINE_SIZE];
    char last_ui_line[TS_LINE_SIZE];
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
    /* Writing (Phase 2) */
    bool does_rw_mount_succeed;
    uint32_t rw_mount_error;
    int rw_mount_calls;
    int commit_calls;
    int fail_commit_at;           /* 1-based commit call that fails; 0 never */
    uint32_t commit_error;
    bool is_space_known;
    int64_t data_bytes;           /* what read_save_space reports */
    int64_t journal_bytes;
    int home_block_calls;
    int home_unblock_calls;
    bool is_home_blocked;
    bool is_mounted;              /* a mount (either kind) is active */
    int probe_results_count;      /* when > 0: probe_results answer the probes in order, then probe */
    game_process_info_t probe_results[MAX_SCRIPTED_ANSWERS];
    int fail_rw_mount_at;         /* 1-based read-write mount call that fails with rw_mount_error; 0 never */
    struct faulty_files *watched_files; /* when set, every commit checks it: open files, bytes since the last commit */
    int commits_with_open_files;
    fake_world_t *world;
    console_port_t port;
} fake_console_t;

/* One account "Player", game not running, mounts succeed with an empty root, space unknown, commits succeed. */
void fake_console_init(fake_console_t *console, fake_world_t *world);

/* File operations that work like stdio until the step chosen here fails (errno = ENOSPC). Every call is counted. */
typedef enum { FAULT_NONE, FAULT_OPEN, FAULT_WRITE, FAULT_CLOSE, FAULT_MAKE_DIR, FAULT_REMOVE_FILE, FAULT_REMOVE_DIR, FAULT_RENAME } fault_step_t;

typedef struct faulty_files {
    fault_step_t fail_step;
    int fail_at;                  /* 1-based call of fail_step that fails; later calls work again */
    const char *only_under;       /* when set, only calls on paths starting with this prefix count and fail */
    int calls[FAULT_RENAME + 1];
    int mutations;                /* successful writes, mkdirs, removes and renames */
    int open_handles;
    int64_t bytes_since_commit;   /* reset by a fake console commit that watches these files */
    int64_t max_bytes_between_commits;
    file_ops_t ops;
} faulty_files_t;

void faulty_files_init(faulty_files_t *files);

/* Fills a response the way the curl adapter would. */
void test_fill_response(http_response_t *resp, const scripted_reply_t *reply);

/* A valid config pointing at 192.168.1.10:8765 with the contract token. */
void test_make_config(config_t *cfg);


/* True when every byte is printable ASCII (0x20-0x7E) or a newline. */
bool test_is_ascii_text(const char *text);

#endif
