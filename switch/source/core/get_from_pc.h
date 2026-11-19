#ifndef TS_GET_FROM_PC_H
#define TS_GET_FROM_PC_H

#include "config.h"
#include "ports.h"
#include "sd_backup.h"

typedef enum {
    PULL_OK,                  /* restored and the PC recorded the sync */
    PULL_OK_ACK_FAILED,       /* restored, but POST /save/ack did not go through: a warning, not an error */
    PULL_GAME_RUNNING,        /* before any change: TotK runs or is suspended */
    PULL_GAME_CHECK_FAILED,
    PULL_NO_SAVE_ON_CONSOLE,
    PULL_CANCELLED,           /* the account list was left, or B at the restore question */
    PULL_DOWNLOAD_FAILED,     /* no usable save from the PC: network, token, refusal */
    PULL_INVALID_ZIP,         /* the PC save broke a "valid save ZIP" rule */
    PULL_DOES_NOT_FIT,        /* bigger than the Switch save */
    PULL_BACKUP_FAILED,       /* the before-pull SD backup could not be made or verified */
    PULL_MOUNT_FAILED,        /* the read-write mount failed */
    PULL_WRITE_FAILED,        /* writing failed before anything changed */
    PULL_RESTORED_BACKUP,     /* writing failed after a change; A put the before-pull backup back */
    PULL_LEFT_INCOMPLETE,     /* writing failed after a change; B left the save as it was */
    PULL_RESTORE_FAILED       /* writing failed, and putting the backup back failed too */
} pull_kind_t;

typedef struct {
    pull_kind_t kind;
    char message[2 * TS_LINE_SIZE];  /* what the user reads; ASCII, no token */
    char backup[SD_BACKUP_NAME_SIZE]; /* the before-pull backup, once made */
} pull_result_t;

/* "X - Get from PC". Order: check the game -> pick the account -> GET /save -> validate the ZIP -> ask "Restore? A/B"
   -> check the game again -> compare with the Switch save size -> before-pull SD backup (verified) -> check the game
   again -> HOME locked -> read-write mount -> write with commits -> unmount -> HOME unlocked -> POST /save/ack.
   Nothing is written before the backup is verified. When writing fails after something changed, the user chooses
   between restoring the backup (the same write path) and leaving the save as is. The save is unmounted on every path. */
void get_from_pc(const sync_env_t *env, const config_t *cfg, pull_result_t *out);

#endif
