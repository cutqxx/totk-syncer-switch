#include "save_fs.h"

#include <stdio.h>
#include <switch.h>

#include "ts_limits.h"

#define MOUNT_NAME "totk"

bool save_fs_mount_readonly(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code)
{
    (void)ctx;
    AccountUid uid = { { account->uid[0], account->uid[1] } };
    Result result = fsdevMountSaveDataReadOnly(MOUNT_NAME, TS_TOTK_TITLE_ID, uid);
    if (R_FAILED(result)) {
        *error_code = (uint32_t)result;
        return false;
    }
    snprintf(root_out, root_size, "%s:", MOUNT_NAME);
    return true;
}

void save_fs_unmount(void *ctx)
{
    (void)ctx;
    fsdevUnmountDevice(MOUNT_NAME);
}
