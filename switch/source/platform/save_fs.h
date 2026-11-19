#ifndef TS_SAVE_FS_H
#define TS_SAVE_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "account.h"

/* console_port_t.mount_save_readonly: fsdevMountSaveDataReadOnly as "totk"; root_out becomes "totk:". A failed read-only
   mount is reported, never worked around. "Send to PC" and every backup use only this mount. */
bool save_fs_mount_readonly(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code);

/* console_port_t.mount_save_readwrite: the writable libnx save mount as "totk". Only "Get from PC" and the restore of an SD backup
   mount the save this way, after a verified SD backup. */
bool save_fs_mount_readwrite(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code);

/* console_port_t.commit_save: commits the "totk" device. */
bool save_fs_commit(void *ctx, uint32_t *error_code);

/* console_port_t.unmount_save: fsdevUnmountDevice("totk"). Uncommitted writes are dropped by the system. */
void save_fs_unmount(void *ctx);

/* console_port_t.read_save_space: data and journal size from the save's extra data (the account's TotK save). */
bool save_fs_read_space(void *ctx, const account_t *account, int64_t *data_bytes, int64_t *journal_bytes);

#endif
