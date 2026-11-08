#ifndef TS_SAVE_FS_H
#define TS_SAVE_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "account.h"

/* console_port_t.mount_save_readonly: fsdevMountSaveDataReadOnly as "totk"; root_out becomes "totk:". There is no
   write, commit or read-write mount anywhere in this app yet; a failed read-only mount is reported, never worked around. */
bool save_fs_mount_readonly(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code);

/* console_port_t.unmount_save: fsdevUnmountDevice("totk"). */
void save_fs_unmount(void *ctx);

#endif
