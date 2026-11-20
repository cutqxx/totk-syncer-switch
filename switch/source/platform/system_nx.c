#include "system_nx.h"

#include <string.h>
#include <switch.h>

#include "ts_limits.h"

#define PM_MODULE 15
#define PM_PROCESS_NOT_FOUND 1

static bool is_pm_process_not_found(Result result)
{
    return R_MODULE(result) == PM_MODULE && R_DESCRIPTION(result) == PM_PROCESS_NOT_FOUND;
}

void system_nx_init(void)
{
    pmdmntInitialize();
    accountInitialize(AccountServiceType_Application);
}

void system_nx_exit(void)
{
    accountExit();
    pmdmntExit();
}

void system_nx_find_game_process(void *ctx, game_process_info_t *out)
{
    (void)ctx;
    u64 pid = 0;
    svcGetProcessId(&out->self_pid, CUR_PROCESS_HANDLE);
    Result result = pmdmntGetProcessId(&pid, TS_TOTK_TITLE_ID);
    out->pid = pid;
    out->is_found = R_SUCCEEDED(result);
    out->is_probe_ok = R_SUCCEEDED(result) || is_pm_process_not_found(result);
    out->error_code = out->is_probe_ok ? 0 : (uint32_t)result;
}

static void read_nickname(AccountUid uid, char nickname[0x21])
{
    AccountProfile profile;
    AccountProfileBase base;
    nickname[0] = '\0';
    if (R_FAILED(accountGetProfile(&profile, uid))) {
        return;
    }
    if (R_SUCCEEDED(accountProfileGet(&profile, NULL, &base))) {
        memcpy(nickname, base.nickname, sizeof base.nickname);
        nickname[sizeof base.nickname] = '\0';
    }
    accountProfileClose(&profile);
}

static bool is_totk_account_save(const FsSaveDataInfo *info)
{
    return info->save_data_type == FsSaveDataType_Account && info->application_id == TS_TOTK_TITLE_ID;
}

static void append_account(const FsSaveDataInfo *info, account_t *out, int index)
{
    char nickname[0x21];
    uint64_t uid[2] = { info->uid.uid[0], info->uid.uid[1] };
    read_nickname(info->uid, nickname);
    account_make(&out[index], uid, nickname, index + 1);
}

int system_nx_list_accounts_with_save(void *ctx, account_t *out, int max)
{
    (void)ctx;
    FsSaveDataInfoReader reader;
    FsSaveDataInfo info;
    s64 total = 0;
    int count = 0;
    if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
        return 0;
    }
    while (count < max && R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, &info, 1, &total)) && total > 0) {
        if (is_totk_account_save(&info)) {
            append_account(&info, out, count++);
        }
    }
    fsSaveDataInfoReaderClose(&reader);
    return count;
}

void system_nx_set_home_blocked(void *ctx, bool is_blocked)
{
    (void)ctx;
    if (is_blocked) {
        appletBeginBlockingHomeButton(0);
    } else {
        appletEndBlockingHomeButton();
    }
}
