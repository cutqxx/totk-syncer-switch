#ifndef TS_SAVE_STEPS_H
#define TS_SAVE_STEPS_H

#include <stddef.h>
#include <stdint.h>

#include "ports.h"
#include "save_pack.h"

/* The console steps every scenario shares. On failure message holds the user text, ending with outcome
   (for example "Nothing was sent"), and nothing else happened. */
typedef enum {
    STEPS_OK,
    STEPS_GAME_RUNNING,      /* TotK runs or is suspended */
    STEPS_GAME_CHECK_FAILED, /* the process check itself failed: handled like a running game */
    STEPS_NO_SAVE_ON_CONSOLE,
    STEPS_CANCELLED,         /* the user left the account list */
    STEPS_MOUNT_FAILED,
    STEPS_PACK_FAILED        /* too large, too many entries, bad name, unreadable file, out of memory */
} steps_status_t;

/* Shows "Checking that TotK is closed..." and asks the console. */
steps_status_t save_steps_check_game(const sync_env_t *env, const char *outcome, char *message, size_t message_size);

/* 0 accounts: STEPS_NO_SAVE_ON_CONSOLE; 1: chosen without asking; several: the list under title. */
steps_status_t save_steps_pick_account(const sync_env_t *env, const char *title, const char *outcome, account_t *account,
                                       char *message, size_t message_size);

/* Mounts the save read-only, packs it into *zip and unmounts it again before returning (whatever happened).
   Shows "Reading save of ...", then the file count and the ZIP size. */
steps_status_t save_steps_read_save(const sync_env_t *env, const account_t *account, int64_t max_save_bytes,
                                    const char *outcome, body_t *zip, pack_stats_t *stats, char *message,
                                    size_t message_size);

/* User text for a failed pack, ending with outcome. */
void save_steps_describe_pack_failure(pack_status_t status, const pack_stats_t *stats, int64_t max_save_bytes,
                                      const char *outcome, char *out, size_t out_size);

/* Shows one formatted line with ui->show_line. */
void save_steps_show(const sync_env_t *env, const char *format, ...) __attribute__((format(printf, 2, 3)));

#endif
