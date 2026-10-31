// TotK Save Sync - Switch side.
// Push the TotK save to Yandex Disk (WebDAV) and pull the PC's save back.
// UI is English-only: the libnx console font has no Cyrillic glyphs.
#include <switch.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backups.h"
#include "config.h"
#include "log.h"
#include "propfind.h"
#include "save_switch.h"
#include "savemeta.h"
#include "savezip.h"
#include "util.h"
#include "webdav.h"

#define APP_VERSION "1.0.0"
#define APP_DIR "sdmc:/switch/totk-sync"
#define BACKUP_DIR APP_DIR "/backups"
#define LOG_PATH APP_DIR "/log.txt"
#define TEST_FILE APP_DIR "/test.zip"
#define CFG_PATH "sdmc:/config/totk-sync/config.json"
#define JKSV_CFG_PATH "sdmc:/config/JKSV/webdav.json"
#define CA_PATH "sdmc:/config/totk-sync/cacert.pem"

#define C_RESET "\x1b[0m"
#define C_RED "\x1b[31;1m"
#define C_GREEN "\x1b[32;1m"
#define C_YELLOW "\x1b[33;1m"
#define C_CYAN "\x1b[36;1m"

static PadState g_pad;
static Config g_cfg;
static ConfigResult g_cfg_res;
static SwUser g_users[ACC_USER_LIST_SIZE];
static int g_user_count;
static int g_user_idx = -1;
static bool g_quit;

// ---------------------------------------------------------------------------
// UI helpers
// ---------------------------------------------------------------------------

static u64 wait_buttons(u64 mask) {
    while (appletMainLoop()) {
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad) & mask;
        if (down) return down;
        consoleUpdate(NULL);
    }
    g_quit = true;
    return 0;
}

static bool confirm(void) {
    printf("\n  " C_CYAN "A" C_RESET " = yes    " C_CYAN "B" C_RESET " = cancel\n");
    consoleUpdate(NULL);
    return wait_buttons(HidNpadButton_A | HidNpadButton_B) == HidNpadButton_A;
}

static void press_to_continue(void) {
    printf("\nPress A to return to the menu.\n");
    consoleUpdate(NULL);
    wait_buttons(HidNpadButton_A | HidNpadButton_B);
}

static void say(const char *color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(const char *color, const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    printf("%s%s" C_RESET "\n", color ? color : "", msg);
    log_msg("UI: %s", msg);
    consoleUpdate(NULL);
}

static void step(const char *msg) {
    printf("- %s\n", msg);
    log_msg("step: %s", msg);
    consoleUpdate(NULL);
}

static double g_progress_last;
static void on_progress(void *ctx, int64_t done, int64_t total) {
    (void)ctx;
    double t = now_seconds();
    if (t - g_progress_last < 0.25 && done != total) return;
    g_progress_last = t;
    if (total > 0)
        printf("\r    %6.2f / %6.2f MB   ", done / 1048576.0, total / 1048576.0);
    else
        printf("\r    %6.2f MB   ", done / 1048576.0);
    consoleUpdate(NULL);
}

static void progress_done(void) {
    printf("\n");
    g_progress_last = 0;
}

static void header(const char *title) {
    consoleClear();
    printf(C_CYAN "TotK Save Sync v" APP_VERSION C_RESET "  -  %s\n", title);
    printf("------------------------------------------------------------\n");
}

static void fmt_local(time_t t, char *out, size_t n) {
    struct tm tm;
    local_time(t, &tm);
    snprintf(out, n, "%02d.%02d.%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour,
             tm.tm_min);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

static bool need_config(void) {
    if (g_cfg_res == CONFIG_OK) return true;
    say(C_RED, "Config problem: %s.", config_result_str(g_cfg_res));
    printf("Create %s\n(see config.example.json) or set up WebDAV in JKSV\n(%s).\n", CFG_PATH, JKSV_CFG_PATH);
    return false;
}

static void choose_user(void) {
    g_user_count = sw_list_users(g_users, ACC_USER_LIST_SIZE);
    if (g_user_count <= 0) {
        g_user_idx = -1;
        return;
    }
    if (g_user_count == 1) {
        g_user_idx = 0;
        return;
    }
    int sel = g_user_idx >= 0 ? g_user_idx : 0;
    for (;;) {
        header("Choose user");
        for (int i = 0; i < g_user_count; i++)
            printf("%s %s" C_RESET "\n", i == sel ? C_CYAN " >" : "  ", g_users[i].nickname);
        printf("\nUp/Down - move, A - choose\n");
        consoleUpdate(NULL);
        u64 k = wait_buttons(HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_A);
        if (g_quit) return;
        if (k & HidNpadButton_AnyUp) sel = (sel + g_user_count - 1) % g_user_count;
        if (k & HidNpadButton_AnyDown) sel = (sel + 1) % g_user_count;
        if (k & HidNpadButton_A) break;
    }
    g_user_idx = sel;
}

static bool need_user(void) {
    if (g_user_idx < 0) choose_user();
    if (g_user_idx < 0) {
        say(C_RED, "No console users found.");
        return false;
    }
    printf("User: %s\n", g_users[g_user_idx].nickname);
    log_msg("user: %s", g_users[g_user_idx].nickname);
    return true;
}

static bool need_game_closed(void) {
    GameState st = sw_totk_state();
    if (st == GAME_RUNNING) {
        say(C_RED, "TotK is running or suspended.");
        printf("Close the game first (HOME, then X on the game) and try again.\n");
        return false;
    }
    if (st == GAME_CHECK_FAILED) {
        say(C_RED, "Cannot check whether TotK is running - refusing to continue.");
        return false;
    }
    return true;
}

static bool need_internet(void) {
    NifmInternetConnectionType type;
    u32 strength = 0;
    NifmInternetConnectionStatus status;
    Result rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
    if (R_FAILED(rc) || status != NifmInternetConnectionStatus_Connected) {
        say(C_RED, "No internet connection.");
        return false;
    }
    return true;
}

static void report_dav_error(const char *what, const DavResult *r) {
    say(C_RED, "%s failed: %s", what, dav_explain(r));
    printf("  HTTP %ld, curl %d%s%s\n", r->http_code, r->curl_code, r->curl_err[0] ? ": " : "", r->curl_err);
}

// Zips the currently mounted-free save (mounts read-only, unmounts after).
static bool zip_current_save(const SwSave *save, Buffer *zip, char *err, size_t err_size) {
    SaveMeta meta;
    sw_fill_meta(save, &meta);
    uint8_t mb[SAVE_META_SIZE];
    savemeta_encode(&meta, mb);
    if (!sw_mount(save, true, err, err_size)) return false;
    bool ok = savezip_build(SAVE_ROOT, mb, zip, err, err_size);
    sw_unmount();
    return ok;
}

// ---------------------------------------------------------------------------
// Push: Switch -> Disk
// ---------------------------------------------------------------------------

static void do_push(void) {
    header("Send to PC");
    char err[256];
    if (!need_config() || !need_user() || !need_game_closed()) return;

    SwSave save;
    if (!sw_find_save(g_users[g_user_idx].uid, &save, err, sizeof err)) {
        say(C_RED, "%s", err);
        return;
    }
    step("Packing the save...");
    Buffer zip = {0};
    if (!zip_current_save(&save, &zip, err, sizeof err)) {
        say(C_RED, "Packing failed: %s", err);
        buf_free(&zip);
        return;
    }
    printf("    ZIP: %.2f MB\n", zip.size / 1048576.0);

    if (!need_internet()) {
        buf_free(&zip);
        return;
    }
    step("Checking the Disk folder...");
    DavResult r;
    if (!dav_ensure_folder(&g_cfg, &r)) {
        report_dav_error("Folder check", &r);
        buf_free(&zip);
        return;
    }

    char ts[32], final_name[96], part_name[104];
    timestamp_now(ts, sizeof ts);
    snprintf(final_name, sizeof final_name, "SW_%s.zip", ts);
    snprintf(part_name, sizeof part_name, "%s.part", final_name);

    double t0 = now_seconds();
    bool done = false;
    if (g_cfg.use_temp_name) {
        step("Uploading...");
        r = dav_put(&g_cfg, part_name, zip.data, zip.size, NULL);
        progress_done();
        if (!dav_ok(&r)) {
            report_dav_error("Upload", &r);
            buf_free(&zip);
            return;
        }
        step("Renaming to the final name...");
        r = dav_move(&g_cfg, part_name, final_name);
        if (dav_ok(&r)) {
            done = true;
        } else {
            // Spec allows uploading straight to the final name if MOVE misbehaves.
            say(C_YELLOW, "Rename failed (HTTP %ld, curl %d). Uploading under the final name.", r.http_code,
                r.curl_code);
            printf("    (a leftover %s stays on the Disk; nothing is deleted)\n", part_name);
        }
    }
    if (!done) {
        step("Uploading...");
        r = dav_put(&g_cfg, final_name, zip.data, zip.size, NULL);
        progress_done();
        if (!dav_ok(&r)) {
            report_dav_error("Upload", &r);
            buf_free(&zip);
            return;
        }
    }
    double secs = now_seconds() - t0;
    say(C_GREEN, "Sent: %s", final_name);
    printf("    %.2f MB in %.1f s\n", zip.size / 1048576.0, secs);
    printf("\nOn the PC, start the launcher - it will pick this save up.\n");
    buf_free(&zip);
}

// ---------------------------------------------------------------------------
// Pull: Disk -> Switch
// ---------------------------------------------------------------------------

static bool find_newest(const char *prefix, DavEntry *out, DavResult *r) {
    Buffer body = {0};
    *r = dav_propfind(&g_cfg, NULL, 1, &body);
    if (!dav_ok(r)) {
        buf_free(&body);
        return false;
    }
    DavList l;
    bool found = false;
    if (body.data && propfind_parse((const char *)body.data, &l)) {
        const DavEntry *e = davlist_newest(&l, prefix, ".zip");
        if (e) {
            *out = *e;
            found = true;
        }
        davlist_free(&l);
    }
    buf_free(&body);
    return found;
}

static bool write_zip_to_save(const SwSave *save, const uint8_t *data, size_t size, const SaveZipInfo *info,
                              char *err, size_t err_size) {
    if (!sw_mount(save, false, err, err_size)) return false;
    bool ok = sw_clear_mounted(err, err_size);
    if (ok) {
        s64 journal = save->extra.journal_size;
        // Leave headroom like JKSV; never 0 so we still commit mid-file on tiny journals.
        if (journal > 4 * 1024 * 1024) journal -= 1024 * 1024;
        else if (journal > 0) journal /= 2;
        CommitHooks hooks = {sw_commit, NULL, journal};
        ok = savezip_extract(data, size, info, SAVE_ROOT, &hooks, err, err_size);
    }
    sw_unmount();
    return ok;
}

static void do_pull(void) {
    header("Get from PC");
    char err[256];
    if (!need_config() || !need_user() || !need_game_closed()) return;

    SwSave save;
    if (!sw_find_save(g_users[g_user_idx].uid, &save, err, sizeof err)) {
        say(C_RED, "%s", err);
        return;
    }
    if (!need_internet()) return;

    step("Looking for the newest PC save...");
    DavEntry pc;
    DavResult r;
    if (!find_newest("PC_", &pc, &r)) {
        if (!dav_ok(&r) && r.http_code != 404) report_dav_error("Listing", &r);
        else say(C_YELLOW, "No PC_*.zip on the Disk - nothing to get.");
        return;
    }
    char when[32];
    fmt_local(pc.modified, when, sizeof when);
    printf("\nFound " C_CYAN "%s" C_RESET "\n  uploaded %s", pc.name, when);
    if (pc.size >= 0) printf(", %.2f MB", pc.size / 1048576.0);
    printf("\n\n" C_YELLOW "The Switch save of %s will be replaced." C_RESET "\n", g_users[g_user_idx].nickname);
    printf("A backup of the current save is written to the SD card first.\nRestore it?\n");
    if (!confirm()) {
        say(NULL, "Cancelled. Nothing changed.");
        return;
    }

    step("Downloading...");
    Buffer zip = {0};
    r = dav_get(&g_cfg, pc.name, &zip);
    progress_done();
    if (!dav_ok(&r)) {
        report_dav_error("Download", &r);
        buf_free(&zip);
        return;
    }

    step("Checking the ZIP...");
    SaveZipInfo info;
    if (!savezip_validate(zip.data, zip.size, &info, err, sizeof err)) {
        say(C_RED, "Rejected: %s", err);
        printf("The save was NOT changed.\n");
        buf_free(&zip);
        return;
    }
    printf("    %u files, %.2f MB%s\n", info.file_count, info.total_size / 1048576.0,
           info.has_meta ? "" : " (no JKSV meta)");

    // The user may have started the game while we were downloading.
    if (!need_game_closed()) {
        buf_free(&zip);
        return;
    }

    step("Backing up the current save to SD...");
    Buffer backup = {0};
    char ts[32], backup_path[256];
    timestamp_now(ts, sizeof ts);
    snprintf(backup_path, sizeof backup_path, BACKUP_DIR "/%s" BACKUP_SUFFIX, ts);
    if (!zip_current_save(&save, &backup, err, sizeof err) || !mkdirs(BACKUP_DIR) ||
        !file_write_atomic(backup_path, backup.data, backup.size)) {
        say(C_RED, "Backup failed%s%s. The save was NOT changed.", err[0] ? ": " : "", err);
        buf_free(&zip);
        buf_free(&backup);
        return;
    }
    printf("    %s\n", backup_path + strlen("sdmc:"));
    backups_prune(BACKUP_DIR, g_cfg.keep_backups);

    if (info.has_meta && !sw_extend_if_needed(&save, &info.meta, err, sizeof err)) {
        say(C_RED, "%s The save was NOT changed.", err);
        buf_free(&zip);
        buf_free(&backup);
        return;
    }

    step("Writing the save (do not turn off the console)...");
    appletBeginBlockingHomeButtonShortAndLongPressed(0);
    bool ok = write_zip_to_save(&save, zip.data, zip.size, &info, err, sizeof err);
    if (!ok) {
        say(C_RED, "Writing failed: %s", err);
        step("Rolling back from the SD backup...");
        char err2[256] = "";
        SaveZipInfo binfo;
        bool rolled = savezip_validate(backup.data, backup.size, &binfo, err2, sizeof err2) &&
                      write_zip_to_save(&save, backup.data, backup.size, &binfo, err2, sizeof err2);
        if (rolled)
            say(C_YELLOW, "Rolled back: the save is as it was before.");
        else
            say(C_RED, "Rollback failed (%s). Restore %s with JKSV.", err2, backup_path + strlen("sdmc:"));
    }
    appletEndBlockingHomeButtonShortAndLongPressed();

    if (ok) {
        say(C_GREEN, "Done: %s is now the Switch save (committed).", pc.name);
        printf("You can start TotK.\n");
    }
    buf_free(&zip);
    buf_free(&backup);
}

// ---------------------------------------------------------------------------
// Status / connection check
// ---------------------------------------------------------------------------

static void do_status(void) {
    header("Check connection");
    if (!need_config()) return;
    printf("Config:   %s\nServer:   %s\nFolder:   /%s\nLogin:    %s\n", g_cfg.source, g_cfg.origin,
           g_cfg.basepath, g_cfg.username);
    if (g_cfg.cainfo[0]) printf("CA file:  %s\n", g_cfg.cainfo);
    printf("\n");
    if (!need_internet()) return;

    step("PROPFIND on the folder...");
    Buffer body = {0};
    DavResult r = dav_propfind(&g_cfg, NULL, 1, &body);
    printf("    HTTP %ld, curl %d, %.1f s\n", r.http_code, r.curl_code, r.seconds);
    if (!dav_ok(&r)) {
        report_dav_error("Connection check", &r);
        if (r.http_code == 404) printf("The folder will be created on the first 'Send to PC'.\n");
        buf_free(&body);
        return;
    }
    say(C_GREEN, "Connection OK.");
    DavList l;
    if (body.data && propfind_parse((const char *)body.data, &l)) {
        const DavEntry *sw = davlist_newest(&l, "SW_", ".zip");
        const DavEntry *pc = davlist_newest(&l, "PC_", ".zip");
        char when[32];
        printf("\n");
        if (sw) {
            fmt_local(sw->modified, when, sizeof when);
            printf("Newest Switch save: %s (%s)\n", sw->name, when);
        } else {
            printf("Newest Switch save: none\n");
        }
        if (pc) {
            fmt_local(pc->modified, when, sizeof when);
            printf("Newest PC save:     %s (%s)\n", pc->name, when);
        } else {
            printf("Newest PC save:     none\n");
        }
        if (pc && (!sw || pc->modified > sw->modified))
            say(C_YELLOW, "\nThe Disk has a newer PC save -> use 'Get from PC'.");
        davlist_free(&l);
    }
    buf_free(&body);
}

// ---------------------------------------------------------------------------
// Phase 0: upload diagnostics
// ---------------------------------------------------------------------------

typedef struct {
    const char *label;
    bool use_ru;
    PutOptions opt;
} Variant;

static void do_upload_test(void) {
    header("Upload test (diagnostics)");
    if (!need_config()) return;

    Buffer data = {0};
    bool from_file = file_read_all(TEST_FILE, &data) && data.size > 0;
    if (!from_file) {
        buf_free(&data);
        // 4 MiB of incompressible pseudo-random bytes.
        size_t n = 4 * 1024 * 1024;
        if (!buf_reserve(&data, n + 1)) {
            say(C_RED, "Out of memory.");
            return;
        }
        u32 x = 0x12345678;
        for (size_t i = 0; i < n; i++) {
            x ^= x << 13, x ^= x >> 17, x ^= x << 5;
            data.data[i] = (uint8_t)x;
        }
        data.size = n;
    }
    printf("Test data: %s, %.2f MB\n\n", from_file ? TEST_FILE : "generated", data.size / 1048576.0);
    printf("A - spec mode only (Content-Length, no Expect, HTTP/1.1)\n");
    printf("X - all variants (Expect / chunked / webdav.yandex.ru)\n");
    printf("B - back\n");
    consoleUpdate(NULL);
    u64 k = wait_buttons(HidNpadButton_A | HidNpadButton_X | HidNpadButton_B);
    if (!(k & (HidNpadButton_A | HidNpadButton_X)) || !need_internet()) {
        buf_free(&data);
        return;
    }

    static const Variant variants[] = {
        {"spec: length, no Expect", false, {0}},
        {"length + Expect", false, {.send_expect = true}},
        {"chunked, no Expect", false, {.chunked = true}},
        {"chunked + Expect", false, {.chunked = true, .send_expect = true}},
        {"length, small buffer", false, {.upload_buffer = 16 * 1024}},
        {"spec on .ru", true, {0}},
    };
    int count = (k & HidNpadButton_X) ? (int)(sizeof variants / sizeof variants[0]) : 1;

    DavResult r;
    step("Checking the Disk folder...");
    if (!dav_ensure_folder(&g_cfg, &r)) {
        report_dav_error("Folder check", &r);
        buf_free(&data);
        return;
    }
    dav_set_retries(0, 0);
    char ts[32];
    timestamp_now(ts, sizeof ts);
    for (int i = 0; i < count; i++) {
        const Variant *v = &variants[i];
        Config cfg = g_cfg;
        if (v->use_ru) {
            char *p = strstr(cfg.origin, "webdav.yandex.com");
            if (!p) {
                printf("%-26s skipped (origin is not webdav.yandex.com)\n", v->label);
                continue;
            }
            memcpy(p, "webdav.yandex.ru", 16);
            memmove(p + 16, p + 17, strlen(p + 17) + 1);
        }
        char name[96];
        snprintf(name, sizeof name, "TEST_%s_v%d.bin", ts, i + 1);
        printf("%s\n", v->label);
        r = dav_put(&cfg, name, data.data, data.size, &v->opt);
        progress_done();
        double mbps = r.seconds > 0 ? data.size / 1048576.0 / r.seconds : 0;
        printf("  %s HTTP %ld, curl %d, %.1f s, %.2f MB/s" C_RESET "\n", dav_ok(&r) ? C_GREEN : C_RED, r.http_code,
               r.curl_code, r.seconds, mbps);
        if (r.curl_err[0]) printf("  %s\n", r.curl_err);
        log_msg("TEST %s: name=%s size=%zu http=%ld curl=%d (%s) %.1fs", v->label, name, data.size, r.http_code,
                r.curl_code, r.curl_err, r.seconds);
        consoleUpdate(NULL);
    }
    dav_set_retries(2, 3000);
    printf("\nResults are also in %s.\nTEST_*.bin files can be deleted from the Disk by hand.\n", LOG_PATH);
    buf_free(&data);
}

// ---------------------------------------------------------------------------

typedef struct {
    const char *label;
    void (*fn)(void);
} MenuItem;

static void do_change_user(void) {
    g_user_idx = -1;
    choose_user();
    header("User");
    if (g_user_idx >= 0) say(C_GREEN, "Selected: %s", g_users[g_user_idx].nickname);
    else say(C_RED, "No users.");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    mkdirs(APP_DIR);
    log_open(LOG_PATH);
    log_msg("=== totk-sync " APP_VERSION " start ===");

    static const SocketInitConfig sock_cfg = {
        .tcp_tx_buf_size = 0x20000,
        .tcp_rx_buf_size = 0x20000,
        .tcp_tx_buf_max_size = 0x80000,
        .tcp_rx_buf_max_size = 0x80000,
        .udp_tx_buf_size = 0x2400,
        .udp_rx_buf_size = 0xA500,
        .sb_efficiency = 8,
        .num_bsd_sessions = 3,
        .bsd_service_type = BsdServiceType_User,
    };
    Result rc_sock = socketInitialize(&sock_cfg);
    Result rc_nifm = nifmInitialize(NifmServiceType_User);
    Result rc_acc = accountInitialize(AccountServiceType_Administrator);
    if (R_FAILED(rc_sock) || R_FAILED(rc_nifm) || R_FAILED(rc_acc))
        log_msg("init: socket=0x%X nifm=0x%X account=0x%X", rc_sock, rc_nifm, rc_acc);
    dav_global_init();
    dav_set_progress(on_progress, NULL);

    g_cfg_res = config_load(CFG_PATH, JKSV_CFG_PATH, &g_cfg);
    if (g_cfg_res == CONFIG_OK && !g_cfg.cainfo[0] && file_exists(CA_PATH))
        str_copy(g_cfg.cainfo, sizeof g_cfg.cainfo, CA_PATH);
    log_msg("config: %s (%s), origin=%s basepath=%s", config_result_str(g_cfg_res), g_cfg.source, g_cfg.origin,
            g_cfg.basepath);

    if (R_SUCCEEDED(rc_acc)) choose_user();

    static const MenuItem items[] = {
        {"Send to PC        (Switch -> Disk)", do_push},
        {"Get from PC       (Disk -> Switch)", do_pull},
        {"Check connection / status", do_status},
        {"Upload test       (diagnostics)", do_upload_test},
        {"Change user", do_change_user},
    };
    const int n_items = sizeof items / sizeof items[0];
    int sel = 0;

    while (!g_quit) {
        header("Menu");
        printf("User:   %s\n", g_user_idx >= 0 ? g_users[g_user_idx].nickname : "(none)");
        printf("Config: %s\n\n", g_cfg_res == CONFIG_OK ? g_cfg.source : config_result_str(g_cfg_res));
        for (int i = 0; i < n_items; i++)
            printf("%s %s" C_RESET "\n", i == sel ? C_CYAN " >" : "  ", items[i].label);
        printf("\nUp/Down - move   A - select   + - exit\n");
        consoleUpdate(NULL);

        u64 k = wait_buttons(HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_A | HidNpadButton_Plus);
        if (g_quit || (k & HidNpadButton_Plus)) break;
        if (k & HidNpadButton_AnyUp) sel = (sel + n_items - 1) % n_items;
        if (k & HidNpadButton_AnyDown) sel = (sel + 1) % n_items;
        if (k & HidNpadButton_A) {
            items[sel].fn();
            if (!g_quit) press_to_continue();
        }
    }

    log_msg("=== exit ===");
    dav_global_cleanup();
    accountExit();
    nifmExit();
    socketExit();
    log_close();
    consoleExit(NULL);
    return 0;
}
