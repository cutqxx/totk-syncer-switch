// Switch-only: users, game-running check, mounting and writing the TotK save.
#pragma once

#include <switch.h>
#include <stdbool.h>

#include "savemeta.h"
#include "savezip.h"

#define SAVE_MOUNT "totk"
#define SAVE_ROOT "totk:/"

typedef struct {
    AccountUid uid;
    char nickname[0x21];
} SwUser;

// Lists console users (max `max`). Returns count, or -1 on error.
int sw_list_users(SwUser *out, int max);

typedef enum {
    GAME_NOT_RUNNING = 0,
    GAME_RUNNING,
    GAME_CHECK_FAILED,
} GameState;

// Is TotK running or suspended in the background?
GameState sw_totk_state(void);

typedef struct {
    FsSaveDataInfo info;
    FsSaveDataExtraData extra;
} SwSave;

// Finds the TotK account save of this user and reads its extra data.
// Returns false (with message) if it doesn't exist.
bool sw_find_save(AccountUid uid, SwSave *out, char *err, size_t err_size);

// Fills a JKSV-compatible meta from the save's extra data.
void sw_fill_meta(const SwSave *save, SaveMeta *meta);

// Mounts the save at "totk:/". read_only uses the read-only open when available.
bool sw_mount(const SwSave *save, bool read_only, char *err, size_t err_size);
void sw_unmount(void);
// Commit callback for savezip_extract (ctx unused).
bool sw_commit(void *ctx);
// Deletes everything inside the mounted save and commits.
bool sw_clear_mounted(char *err, size_t err_size);
// Grows the save if `meta` says it needs to be bigger (as JKSV does). Must be unmounted.
bool sw_extend_if_needed(const SwSave *save, const SaveMeta *meta, char *err, size_t err_size);
