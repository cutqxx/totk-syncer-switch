#ifndef TS_BACKUP_TO_SD_H
#define TS_BACKUP_TO_SD_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "ports.h"
#include "sd_backup.h"

typedef enum {
    BACKUP_OK,
    BACKUP_GAME_RUNNING,
    BACKUP_GAME_CHECK_FAILED,
    BACKUP_NO_SAVE_ON_CONSOLE,
    BACKUP_CANCELLED,
    BACKUP_MOUNT_FAILED,
    BACKUP_PACK_FAILED,
    BACKUP_SD_FAILED
} backup_kind_t;

typedef struct {
    backup_kind_t kind;
    char message[TS_LINE_SIZE];   /* "BACKUP OK: <path>" or the error; ASCII */
    char name[SD_BACKUP_NAME_SIZE];
} backup_result_t;

/* Reads the save the way "Send to PC" does (game check, account, read-only mount, pack, unmount) and writes the ZIP as
   a manual SD backup with sd_backup_write (the Y menu item); the save itself is only read. */
void backup_save_to_sd(const sync_env_t *env, const config_t *cfg, int64_t max_save_bytes, backup_result_t *out);

/* Packs the already chosen account's save and writes it as a before-pull backup. The game check is the caller's. */
backup_kind_t backup_account_before_pull(const sync_env_t *env, const config_t *cfg, const account_t *account,
                                         int64_t max_save_bytes, char *name_out, char *message, size_t message_size);

#endif
