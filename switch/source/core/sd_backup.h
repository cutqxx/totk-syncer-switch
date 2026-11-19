#ifndef TS_SD_BACKUP_H
#define TS_SD_BACKUP_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "body.h"
#include "file_ops.h"
#include "ts_limits.h"

#define SD_BACKUP_NAME_SIZE 48 /* "totk-YYYYMMDD-HHMMSS-before-pull.zip" + NUL fits */

typedef enum { SD_BACKUP_MANUAL, SD_BACKUP_BEFORE_PULL } sd_backup_reason_t;

typedef struct {
    char name[SD_BACKUP_NAME_SIZE];       /* totk-<YYYYMMDD-HHMMSS>-<manual|before-pull>.zip */
    char display_path[TS_PATH_SIZE];      /* sd:/switch/totk-sync/backups/<name> */
    char error[TS_LINE_SIZE];             /* why it failed: ASCII, names the step and the file */
    int pruned;                           /* old backups deleted after this one was verified */
} sd_backup_result_t;

/* Writes zip to <sd_root>/switch/totk-sync/backups/<name>.part, renames it to <name>, reads it back and checks the CRC
   of every entry. Only then it reports success and prunes: the newest keep totk-*.zip backups stay (the new one is never
   deleted), other files are never touched. On failure the .part (or the unverified .zip) is removed. An existing backup
   with the same name is never overwritten. */
bool sd_backup_write(const file_ops_t *ops, const char *sd_root, const struct tm *now, sd_backup_reason_t reason,
                     const body_t *zip, int keep, sd_backup_result_t *out);

/* Reads a backup made by sd_backup_write into *out (empty body expected). False when it cannot be read. */
bool sd_backup_read(const char *sd_root, const char *name, body_t *out);

/* totk-<8 digits>-<6 digits>-<manual|before-pull>.zip: the only names pruning may delete. */
bool sd_backup_is_own_name(const char *name);

#endif
