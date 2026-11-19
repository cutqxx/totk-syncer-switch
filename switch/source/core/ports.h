#ifndef TS_PORTS_H
#define TS_PORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "account.h"
#include "file_ops.h"
#include "game_guard.h"
#include "http.h"
#include "log.h"

/* perform fills *out completely (also on failure); the caller releases it with http_response_free. */
typedef struct {
    void *ctx;
    void (*perform)(void *ctx, const http_request_t *req, http_response_t *out);
} net_port_t;

typedef struct {
    void *ctx;
    void (*now_local)(void *ctx, struct tm *out);
    void (*sleep_seconds)(void *ctx, int seconds);
} clock_port_t;

typedef struct {
    void *ctx;
    void (*show_line)(void *ctx, const char *line);
    /* Shows title above the list and blocks until the user picks an account; returns its index, or -1 when cancelled. */
    int (*choose_account)(void *ctx, const char *title, const account_t *list, int count);
    /* Shows prompt and blocks until A (true) or B (false) is pressed. */
    bool (*confirm)(void *ctx, const char *prompt);
} ui_port_t;

/* The console side of the save. Reading: probe, list, read-only mount. Writing (Phase 2, "Get from PC" and the restore of
   an SD backup only): read-write mount, commit, the save's size and journal, and the HOME button lock. */
typedef struct {
    void *ctx;
    /* Always fills *out; an out that stays zeroed means "probe failed" and is treated like a running game. */
    void (*find_game_process)(void *ctx, game_process_info_t *out);
    /* Accounts that own a TotK save, nicknames already ASCII; returns how many were stored (at most max). */
    int (*list_accounts_with_save)(void *ctx, account_t *out, int max);
    /* Mounts the account's TotK save read-only and stores its root path ("totk:"); on failure *error_code says why. */
    bool (*mount_save_readonly)(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code);
    void (*unmount_save)(void *ctx);
    /* Mounts the account's TotK save read-write at the same root; on failure *error_code says why. */
    bool (*mount_save_readwrite)(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code);
    /* Commits what was written to the read-write mount. Every file must be closed first. */
    bool (*commit_save)(void *ctx, uint32_t *error_code);
    /* The usable size of the account's save and its journal size, in bytes. False when they cannot be read. */
    bool (*read_save_space)(void *ctx, const account_t *account, int64_t *data_bytes, int64_t *journal_bytes);
    /* true: HOME does nothing until called again with false. */
    void (*set_home_blocked)(void *ctx, bool is_blocked);
} console_port_t;

typedef struct {
    const net_port_t *net;
    const clock_port_t *clock;
    const ui_port_t *ui;
    log_sink_t *log; /* NULL: no logging */
    const console_port_t *console;
    const file_ops_t *files; /* writes to the SD and to the mounted save */
    const char *sd_root;     /* "sdmc:" on the console, a temporary folder in tests */
} sync_env_t;

#endif
