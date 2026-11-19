#include "save_fs.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "ts_limits.h"

#define MOUNT_NAME "totk"

static AccountUid to_uid(const account_t *account)
{
    AccountUid uid = { { account->uid[0], account->uid[1] } };
    return uid;
}

static bool report_mount(Result result, char *root_out, size_t root_size, uint32_t *error_code)
{
    if (R_FAILED(result)) {
        *error_code = (uint32_t)result;
        return false;
    }
    snprintf(root_out, root_size, "%s:", MOUNT_NAME);
    return true;
}

bool save_fs_mount_readonly(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code)
{
    (void)ctx;
    return report_mount(fsdevMountSaveDataReadOnly(MOUNT_NAME, TS_TOTK_TITLE_ID, to_uid(account)), root_out, root_size,
                        error_code);
}

bool save_fs_mount_readwrite(void *ctx, const account_t *account, char *root_out, size_t root_size, uint32_t *error_code)
{
    (void)ctx;
    return report_mount(fsdevMountSaveData(MOUNT_NAME, TS_TOTK_TITLE_ID, to_uid(account)), root_out, root_size, error_code);
}

bool save_fs_commit(void *ctx, uint32_t *error_code)
{
    (void)ctx;
    Result result = fsdevCommitDevice(MOUNT_NAME);
    *error_code = R_FAILED(result) ? (uint32_t)result : 0;
    return R_SUCCEEDED(result);
}

void save_fs_unmount(void *ctx)
{
    (void)ctx;
    fsdevUnmountDevice(MOUNT_NAME);
}

static bool find_save_data_id(const account_t *account, u64 *save_data_id)
{
    FsSaveDataInfoReader reader;
    FsSaveDataInfo info;
    s64 total = 0;
    bool is_found = false;
    if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
        return false;
    }
    while (!is_found && R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, &info, 1, &total)) && total > 0) {
        is_found = info.save_data_type == FsSaveDataType_Account && info.application_id == TS_TOTK_TITLE_ID
                   && info.uid.uid[0] == account->uid[0] && info.uid.uid[1] == account->uid[1];
        if (is_found) {
            *save_data_id = info.save_data_id;
        }
    }
    fsSaveDataInfoReaderClose(&reader);
    return is_found;
}

bool save_fs_read_space(void *ctx, const account_t *account, int64_t *data_bytes, int64_t *journal_bytes)
{
    (void)ctx;
    u64 save_data_id = 0;
    FsSaveDataExtraData extra;
    memset(&extra, 0, sizeof extra);
    if (!find_save_data_id(account, &save_data_id)) {
        return false;
    }
    Result result = fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof extra, FsSaveDataSpaceId_User,
                                                                       save_data_id);
    if (R_FAILED(result) || extra.data_size <= 0) {
        return false;
    }
    *data_bytes = extra.data_size;
    *journal_bytes = extra.journal_size;
    return true;
}
