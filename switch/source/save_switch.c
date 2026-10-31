#include "save_switch.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "util.h"

int sw_list_users(SwUser *out, int max) {
    AccountUid uids[ACC_USER_LIST_SIZE];
    s32 count = 0;
    Result rc = accountListAllUsers(uids, ACC_USER_LIST_SIZE, &count);
    if (R_FAILED(rc)) {
        log_msg("accountListAllUsers failed: 0x%X", rc);
        return -1;
    }
    int n = 0;
    for (s32 i = 0; i < count && n < max; i++) {
        SwUser *u = &out[n++];
        memset(u, 0, sizeof *u);
        u->uid = uids[i];
        AccountProfile prof;
        AccountProfileBase base;
        if (R_SUCCEEDED(accountGetProfile(&prof, uids[i]))) {
            if (R_SUCCEEDED(accountProfileGet(&prof, NULL, &base)))
                str_copy(u->nickname, sizeof u->nickname, base.nickname);
            accountProfileClose(&prof);
        }
        if (!u->nickname[0]) snprintf(u->nickname, sizeof u->nickname, "User %d", (int)i + 1);
    }
    return n;
}

GameState sw_totk_state(void) {
    if (R_FAILED(pmdmntInitialize())) {
        log_msg("pmdmntInitialize failed");
        return GAME_CHECK_FAILED;
    }
    GameState st = GAME_NOT_RUNNING;
    u64 pid = 0;
    // Any process with TotK's program id (running or suspended in the background).
    u64 self_pid = 0;
    svcGetProcessId(&self_pid, CUR_PROCESS_HANDLE);
    // (If this app was started via title override of TotK, the "TotK" process is us.)
    if (R_SUCCEEDED(pmdmntGetProcessId(&pid, TOTK_TITLE_ID)) && pid != 0 && pid != self_pid) st = GAME_RUNNING;
    // Also check the current application process, in case of a different program id variant.
    if (st == GAME_NOT_RUNNING && R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && pid != 0 &&
        pid != self_pid) {
        u64 program_id = 0;
        if (R_SUCCEEDED(pminfoInitialize())) {
            if (R_SUCCEEDED(pminfoGetProgramId(&program_id, pid)) &&
                (program_id & ~0xFFFULL) == (TOTK_TITLE_ID & ~0xFFFULL))
                st = GAME_RUNNING;
            pminfoExit();
        }
    }
    pmdmntExit();
    log_msg("TotK running check: %s", st == GAME_RUNNING ? "RUNNING" : "not running");
    return st;
}

bool sw_find_save(AccountUid uid, SwSave *out, char *err, size_t err_size) {
    memset(out, 0, sizeof *out);
    FsSaveDataInfoReader reader;
    Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        snprintf(err, err_size, "Cannot list saves (0x%X).", rc);
        return false;
    }
    bool found = false;
    FsSaveDataInfo infos[32];
    for (;;) {
        s64 total = 0;
        rc = fsSaveDataInfoReaderRead(&reader, infos, 32, &total);
        if (R_FAILED(rc) || total <= 0) break;
        for (s64 i = 0; i < total; i++) {
            const FsSaveDataInfo *in = &infos[i];
            if (in->application_id == TOTK_TITLE_ID && in->save_data_type == FsSaveDataType_Account &&
                accountUidIsValid(&in->uid) && memcmp(&in->uid, &uid, sizeof uid) == 0) {
                out->info = *in;
                found = true;
                break;
            }
        }
        if (found) break;
    }
    fsSaveDataInfoReaderClose(&reader);
    if (!found) {
        snprintf(err, err_size, "This user has no TotK save on the console. Start the game once first.");
        return false;
    }
    rc = fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&out->extra, sizeof out->extra,
                                                             (FsSaveDataSpaceId)out->info.save_data_space_id,
                                                             out->info.save_data_id);
    if (R_FAILED(rc)) {
        snprintf(err, err_size, "Cannot read save info (0x%X).", rc);
        return false;
    }
    log_msg("save found: id=%016llX size=%lld journal=%lld", (unsigned long long)out->info.save_data_id,
            (long long)out->extra.data_size, (long long)out->extra.journal_size);
    return true;
}

void sw_fill_meta(const SwSave *save, SaveMeta *m) {
    const FsSaveDataExtraData *x = &save->extra;
    memset(m, 0, sizeof *m);
    m->magic = SAVE_META_MAGIC;
    m->revision = 0x01;
    m->application_id = x->attr.application_id;
    memcpy(m->account_id, &x->attr.uid, sizeof m->account_id);
    m->system_save_id = x->attr.system_save_data_id;
    m->save_data_type = x->attr.save_data_type;
    m->save_data_rank = x->attr.save_data_rank;
    m->save_data_index = x->attr.save_data_index;
    m->owner_id = x->owner_id;
    m->timestamp = x->timestamp;
    m->flags = x->flags;
    m->save_data_size = x->data_size;
    m->journal_size = x->journal_size;
    m->commit_id = x->commit_id;
    m->save_data_space_id = save->info.save_data_space_id;
}

static bool g_mounted;

bool sw_mount(const SwSave *save, bool read_only, char *err, size_t err_size) {
    if (g_mounted) sw_unmount();
    FsSaveDataAttribute attr;
    memset(&attr, 0, sizeof attr);
    attr.application_id = TOTK_TITLE_ID;
    attr.uid = save->info.uid;
    attr.save_data_type = FsSaveDataType_Account;
    attr.save_data_rank = save->info.save_data_rank;
    attr.save_data_index = save->info.save_data_index;

    FsFileSystem fs;
    FsSaveDataSpaceId space = (FsSaveDataSpaceId)save->info.save_data_space_id;
    Result rc;
    if (read_only) {
        rc = fsOpenReadOnlySaveDataFileSystem(&fs, space, &attr);
        // Older firmware / unexpected refusal: fall back to the normal open.
        // We still never write or commit on this path.
        if (R_FAILED(rc)) {
            log_msg("read-only open failed (0x%X), using normal open", rc);
            rc = fsOpenSaveDataFileSystem(&fs, space, &attr);
        }
    } else {
        rc = fsOpenSaveDataFileSystem(&fs, space, &attr);
    }
    if (R_FAILED(rc)) {
        snprintf(err, err_size, "Cannot open the save (0x%X). Is the game running?", rc);
        log_msg("open save failed: 0x%X", rc);
        return false;
    }
    if (fsdevMountDevice(SAVE_MOUNT, fs) == -1) {
        fsFsClose(&fs);
        snprintf(err, err_size, "Cannot mount the save.");
        return false;
    }
    g_mounted = true;
    log_msg("save mounted (%s)", read_only ? "read-only" : "read-write");
    return true;
}

void sw_unmount(void) {
    if (!g_mounted) return;
    fsdevUnmountDevice(SAVE_MOUNT);
    g_mounted = false;
}

bool sw_commit(void *ctx) {
    (void)ctx;
    Result rc = fsdevCommitDevice(SAVE_MOUNT);
    if (R_FAILED(rc)) log_msg("commit failed: 0x%X", rc);
    return R_SUCCEEDED(rc);
}

bool sw_clear_mounted(char *err, size_t err_size) {
    FsFileSystem *fs = fsdevGetDeviceFileSystem(SAVE_MOUNT);
    if (!fs) {
        snprintf(err, err_size, "Save is not mounted.");
        return false;
    }
    Result rc = fsFsCleanDirectoryRecursively(fs, "/");
    if (R_FAILED(rc)) {
        snprintf(err, err_size, "Cannot clear the save (0x%X).", rc);
        return false;
    }
    if (!sw_commit(NULL)) {
        snprintf(err, err_size, "Commit after clearing failed.");
        return false;
    }
    return true;
}

bool sw_extend_if_needed(const SwSave *save, const SaveMeta *meta, char *err, size_t err_size) {
    if (meta->save_data_size <= save->extra.data_size) return true;
    s64 journal = meta->journal_size > save->extra.journal_size ? meta->journal_size : save->extra.journal_size;
    log_msg("extending save %lld -> %lld", (long long)save->extra.data_size, (long long)meta->save_data_size);
    Result rc = fsExtendSaveDataFileSystem((FsSaveDataSpaceId)save->info.save_data_space_id,
                                           save->info.save_data_id, meta->save_data_size, journal);
    if (R_FAILED(rc)) {
        snprintf(err, err_size, "Cannot extend the save (0x%X).", rc);
        return false;
    }
    return true;
}
