// Host-side tests for the platform-neutral parts of totk-sync.
// Build & run: make -C switch/tests
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../source/backups.h"
#include "../source/config.h"
#include "../source/propfind.h"
#include "../source/savemeta.h"
#include "../source/savezip.h"
#include "../source/util.h"
#include "../source/webdav.h"
#include "miniz.h"

static int g_fail, g_pass;
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (cond) g_pass++;                                                     \
        else {                                                                  \
            g_fail++;                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                       \
    } while (0)

// The real .nx_save_meta.bin from the spec (§2.3).
static const uint8_t REAL_META[86] = {
    0x4a, 0x4b, 0x53, 0x56, 0x01, 0x00, 0x60, 0x5b, 0x11, 0xc0, 0xf2, 0x00, 0x01, 0x26, 0x61, 0x79,
    0xe6, 0x00, 0x00, 0x00, 0x10, 0xbb, 0x2a, 0xf3, 0xb7, 0x0d, 0x5d, 0xcb, 0xc9, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x60, 0x5b, 0x11, 0xc0, 0xf2, 0x00,
    0x01, 0xae, 0x42, 0xe6, 0x6a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69,
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5e, 0xed, 0x77,
    0x9d, 0x7c, 0x8e, 0xb0, 0x68, 0x01};

static void test_util(void) {
    char out[256];
    CHECK(url_encode_path(out, sizeof out, "JKSV/The Legend of Zelda/SW_1.zip"));
    CHECK(strcmp(out, "JKSV/The%20Legend%20of%20Zelda/SW_1.zip") == 0);
    CHECK(url_encode_path(out, sizeof out, "a&b#c?d"));
    CHECK(strcmp(out, "a%26b%23c%3Fd") == 0);
    CHECK(!url_encode_path(out, 5, "abcdef"));

    char d[] = "The%20Legend%2fx%zz";
    url_decode_inplace(d);
    CHECK(strcmp(d, "The Legend/x%zz") == 0);

    time_t t;
    CHECK(parse_http_date("Sat, 31 Oct 2026 12:34:56 GMT", &t));
    CHECK(t == 1793450096);
    CHECK(parse_http_date("Thu, 01 Jan 1970 00:00:00 GMT", &t) && t == 0);
    CHECK(!parse_http_date("garbage", &t));
    CHECK(utc_mktime(2000, 2, 29, 0, 0, 0) == 951782400);
}

static void test_config(void) {
    Config c;
    const char *ok =
        "{\n \"origin\": \"https://webdav.yandex.com/\",\n"
        " \"basepath\": \"/JKSV/The Legend of Zelda Tears of the Kingdom/\",\n"
        " \"username\": \"LOGIN\", \"password\": \"pa\\\"ss\\\\w\",\n"
        " \"nested\": {\"a\": [1, 2, {\"b\": \"}\"}]},\n"
        " \"keep_backups\": 5, \"use_temp_name\": false }";
    CHECK(config_parse(ok, &c) == CONFIG_OK);
    CHECK(strcmp(c.origin, "https://webdav.yandex.com") == 0);
    CHECK(strcmp(c.basepath, "JKSV/The Legend of Zelda Tears of the Kingdom") == 0);
    CHECK(strcmp(c.username, "LOGIN") == 0);
    CHECK(strcmp(c.password, "pa\"ss\\w") == 0);
    CHECK(c.keep_backups == 5);
    CHECK(c.use_temp_name == false);

    CHECK(config_parse("{\"origin\":\"x\",\"username\":\"u\"}", &c) == CONFIG_MISSING_FIELDS);
    CHECK(config_parse("{\"origin\":\"x\",", &c) == CONFIG_PARSE_ERROR);
    CHECK(config_parse("not json", &c) == CONFIG_PARSE_ERROR);

    // Defaults when optional fields are absent.
    CHECK(config_parse("{\"origin\":\"o\",\"username\":\"u\",\"password\":\"p\"}", &c) == CONFIG_OK);
    CHECK(strcmp(c.basepath, "JKSV/" TOTK_FOLDER_NAME) == 0);
    CHECK(c.keep_backups == 20 && c.use_temp_name);

    // JKSV fallback appends the game folder.
    CHECK(config_parse_jksv("{\"origin\":\"https://webdav.yandex.com\",\"basepath\":\"JKSV/\","
                            "\"username\":\"u\",\"password\":\"p\"}", &c) == CONFIG_OK);
    CHECK(strcmp(c.basepath, "JKSV/" TOTK_FOLDER_NAME) == 0);
    CHECK(config_parse_jksv("{\"origin\":\"o\",\"username\":\"u\",\"password\":\"p\"}", &c) == CONFIG_OK);
    CHECK(strcmp(c.basepath, TOTK_FOLDER_NAME) == 0);
}

static void test_propfind(void) {
    // Shape of a Yandex reply (prefix "d:"), plus an unprefixed entry.
    const char *xml =
        "<?xml version='1.0' encoding='UTF-8'?>"
        "<d:multistatus xmlns:d=\"DAV:\">"
        "<d:response><d:href>/JKSV/The%20Legend%20of%20Zelda%20Tears%20of%20the%20Kingdom/</d:href>"
        "<d:propstat><d:status>HTTP/1.1 200 OK</d:status><d:prop>"
        "<d:resourcetype><d:collection/></d:resourcetype>"
        "<d:getlastmodified>Sat, 31 Oct 2026 10:00:00 GMT</d:getlastmodified></d:prop></d:propstat></d:response>"
        "<d:response><d:href>/JKSV/The%20Legend%20of%20Zelda%20Tears%20of%20the%20Kingdom/PC_2026-10-30_10-00-00.zip</d:href>"
        "<d:propstat><d:status>HTTP/1.1 200 OK</d:status><d:prop><d:resourcetype/>"
        "<d:getcontentlength>12345</d:getcontentlength>"
        "<d:getlastmodified>Fri, 30 Oct 2026 10:00:05 GMT</d:getlastmodified></d:prop></d:propstat></d:response>"
        "<d:response><d:href>/JKSV/The%20Legend%20of%20Zelda%20Tears%20of%20the%20Kingdom/PC_2026-10-31_09-00-00.zip</d:href>"
        "<d:propstat><d:prop><d:resourcetype/><d:getlastmodified>Sat, 31 Oct 2026 09:00:05 GMT</d:getlastmodified>"
        "</d:prop></d:propstat></d:response>"
        "<d:response><d:href>/JKSV/The%20Legend%20of%20Zelda%20Tears%20of%20the%20Kingdom/PC_2026-10-31_09-30-00.zip.part</d:href>"
        "<d:propstat><d:prop><d:resourcetype/><d:getlastmodified>Sat, 31 Oct 2026 11:00:00 GMT</d:getlastmodified>"
        "</d:prop></d:propstat></d:response>"
        "<response xmlns=\"DAV:\"><href>https://webdav.yandex.com/JKSV/x/SW_2026-10-31_12-00-00.zip</href>"
        "<propstat><prop><resourcetype/><getlastmodified>Sat, 31 Oct 2026 12:00:00 GMT</getlastmodified>"
        "</prop></propstat></response>"
        "</d:multistatus>";
    DavList l;
    CHECK(propfind_parse(xml, &l));
    CHECK(l.count == 5);
    if (l.count == 5) {
        CHECK(l.items[0].is_dir);
        CHECK(strcmp(l.items[0].name, "The Legend of Zelda Tears of the Kingdom") == 0);
        CHECK(!l.items[1].is_dir && l.items[1].size == 12345);
        CHECK(strcmp(l.items[4].name, "SW_2026-10-31_12-00-00.zip") == 0);
    }
    const DavEntry *e = davlist_newest(&l, "PC_", ".zip");
    CHECK(e && strcmp(e->name, "PC_2026-10-31_09-00-00.zip") == 0);
    CHECK(davlist_newest(&l, "XX_", ".zip") == NULL);
    davlist_free(&l);

    CHECK(!propfind_parse("<html>nope</html>", &l));
}

static void test_meta(void) {
    SaveMeta m;
    CHECK(savemeta_decode(REAL_META, sizeof REAL_META, &m));
    CHECK(m.application_id == TOTK_TITLE_ID);
    CHECK(m.owner_id == TOTK_TITLE_ID);
    CHECK(m.revision == 1);
    CHECK(m.save_data_type == 1);  // Account
    CHECK(m.save_data_space_id == 1);
    uint8_t back[SAVE_META_SIZE];
    savemeta_encode(&m, back);
    CHECK(memcmp(back, REAL_META, SAVE_META_SIZE) == 0);
    CHECK(!savemeta_decode(REAL_META, 10, &m));
    uint8_t bad[86];
    memcpy(bad, REAL_META, 86);
    bad[0] = 'X';
    CHECK(!savemeta_decode(bad, 86, &m));
}

// --- ZIP helpers -----------------------------------------------------------

static void write_file(const char *path, const char *content, size_t len) {
    FILE *f = fopen(path, "wb");
    fwrite(content, 1, len, f);
    fclose(f);
}

static char g_tmp[512];

static void make_save_tree(const char *root) {
    char p[700];
    mkdirs(root);
    snprintf(p, sizeof p, "%s/slot_00", root); mkdirs(p);
    snprintf(p, sizeof p, "%s/album", root); mkdirs(p);
    snprintf(p, sizeof p, "%s/storage", root); mkdirs(p);
    snprintf(p, sizeof p, "%s/option.sav", root); write_file(p, "OPTION", 6);
    // A larger, compressible-but-not-trivial file.
    static char big[300000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (char)((i * 7919) ^ (i >> 5));
    snprintf(p, sizeof p, "%s/slot_00/progress.sav", root); write_file(p, big, sizeof big);
    snprintf(p, sizeof p, "%s/slot_00/caption.sav", root); write_file(p, "CAP", 3);
    snprintf(p, sizeof p, "%s/album/000_Photo.jpg", root); write_file(p, "JPEG", 4);
    snprintf(p, sizeof p, "%s/storage/CacheStorageKey.dat", root); write_file(p, "123456789", 9);
    // Junk that must be skipped.
    snprintf(p, sizeof p, "%s/desktop.ini", root); write_file(p, "x", 1);
    snprintf(p, sizeof p, "%s/old.zip", root); write_file(p, "x", 1);
    snprintf(p, sizeof p, "%s/.nx_save_meta.bin", root); write_file(p, "x", 1);
}

static bool same_file(const char *a, const char *b) {
    Buffer x = {0}, y = {0};
    bool ok = file_read_all(a, &x) && file_read_all(b, &y) && x.size == y.size &&
              (x.size == 0 || memcmp(x.data, y.data, x.size) == 0);
    buf_free(&x);
    buf_free(&y);
    return ok;
}

static int g_commits;
static bool count_commit(void *ctx) {
    (void)ctx;
    g_commits++;
    return true;
}

static bool zip_mem(const char **names, const char **contents, int n, Buffer *out) {
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_writer_init_heap(&z, 0, 0)) return false;
    for (int i = 0; i < n; i++)
        if (!mz_zip_writer_add_mem(&z, names[i], contents[i], strlen(contents[i]), 6)) return false;
    void *d;
    size_t s;
    if (!mz_zip_writer_finalize_heap_archive(&z, &d, &s)) return false;
    mz_zip_writer_end(&z);
    out->data = d;
    out->size = out->cap = s;
    return true;
}

static void test_zip(void) {
    char src[600], dst[600], err[256];
    snprintf(src, sizeof src, "%s/src", g_tmp);
    snprintf(dst, sizeof dst, "%s/dst", g_tmp);
    make_save_tree(src);

    Buffer zip = {0};
    CHECK(savezip_build(src, REAL_META, &zip, err, sizeof err));

    // Layout: meta first, directory entries present, junk absent.
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    CHECK(mz_zip_reader_init_mem(&z, zip.data, zip.size, 0));
    char name[256];
    mz_zip_reader_get_filename(&z, 0, name, sizeof name);
    CHECK(strcmp(name, ".nx_save_meta.bin") == 0);
    CHECK(mz_zip_reader_locate_file(&z, "slot_00/", NULL, 0) >= 0);
    CHECK(mz_zip_reader_locate_file(&z, "album/", NULL, 0) >= 0);
    CHECK(mz_zip_reader_locate_file(&z, "desktop.ini", NULL, 0) < 0);
    CHECK(mz_zip_reader_locate_file(&z, "old.zip", NULL, 0) < 0);
    CHECK(mz_zip_reader_get_num_files(&z) == 9);  // meta + 3 dirs + 5 files
    mz_zip_reader_end(&z);

    SaveZipInfo info;
    CHECK(savezip_validate(zip.data, zip.size, &info, err, sizeof err));
    CHECK(info.has_meta && info.meta.application_id == TOTK_TITLE_ID);
    CHECK(info.prefix[0] == 0);
    CHECK(info.file_count == 5);

    g_commits = 0;
    CommitHooks hooks = {count_commit, NULL, 100000};  // small journal -> mid-file commits
    CHECK(savezip_extract(zip.data, zip.size, &info, dst, &hooks, err, sizeof err));
    CHECK(g_commits > 6);
    char a[700], b[700];
    const char *files[] = {"option.sav", "slot_00/progress.sav", "slot_00/caption.sav", "album/000_Photo.jpg",
                           "storage/CacheStorageKey.dat"};
    for (int i = 0; i < 5; i++) {
        snprintf(a, sizeof a, "%s/%s", src, files[i]);
        snprintf(b, sizeof b, "%s/%s", dst, files[i]);
        CHECK(same_file(a, b));
    }
    snprintf(b, sizeof b, "%s/.nx_save_meta.bin", dst);
    CHECK(!file_exists(b));
    snprintf(b, sizeof b, "%s/desktop.ini", dst);
    CHECK(!file_exists(b));

    // Corrupt one byte in the middle -> rejected.
    Buffer bad = {0};
    buf_append(&bad, zip.data, zip.size);
    bad.data[zip.size / 2] ^= 0xFF;
    CHECK(!savezip_validate(bad.data, bad.size, &info, err, sizeof err));
    buf_free(&bad);
    CHECK(!savezip_validate((const uint8_t *)"PK\3\4junk", 9, &info, err, sizeof err));
    buf_free(&zip);

    // Missing progress.sav -> rejected.
    {
        const char *n[] = {"option.sav", "slot_01/progress.sav"};
        const char *c[] = {"o", "p"};
        Buffer zb = {0};
        CHECK(zip_mem(n, c, 2, &zb));
        CHECK(!savezip_validate(zb.data, zb.size, &info, err, sizeof err));
        CHECK(strstr(err, "progress.sav") != NULL);
        buf_free(&zb);
    }
    // Zip-slip -> rejected.
    {
        const char *n[] = {"option.sav", "slot_00/progress.sav", "../evil.sav"};
        const char *c[] = {"o", "p", "x"};
        Buffer zb = {0};
        CHECK(zip_mem(n, c, 3, &zb));
        CHECK(!savezip_validate(zb.data, zb.size, &info, err, sizeof err));
        CHECK(strstr(err, "unsafe") != NULL);
        buf_free(&zb);
    }
    // Other game's meta -> rejected.
    {
        uint8_t other[86];
        memcpy(other, REAL_META, 86);
        other[5] ^= 0x10;  // change title id
        mz_zip_archive w;
        memset(&w, 0, sizeof w);
        mz_zip_writer_init_heap(&w, 0, 0);
        mz_zip_writer_add_mem(&w, ".nx_save_meta.bin", other, 86, 6);
        mz_zip_writer_add_mem(&w, "option.sav", "o", 1, 6);
        mz_zip_writer_add_mem(&w, "slot_00/progress.sav", "p", 1, 6);
        void *d;
        size_t s;
        mz_zip_writer_finalize_heap_archive(&w, &d, &s);
        mz_zip_writer_end(&w);
        CHECK(!savezip_validate(d, s, &info, err, sizeof err));
        CHECK(strstr(err, "another game") != NULL);
        free(d);
    }
    // Save wrapped in a folder, no meta -> accepted with prefix.
    {
        const char *n[] = {"backup/option.sav", "backup/slot_00/progress.sav", "readme.txt"};
        const char *c[] = {"o", "p", "r"};
        Buffer zb = {0};
        CHECK(zip_mem(n, c, 3, &zb));
        CHECK(savezip_validate(zb.data, zb.size, &info, err, sizeof err));
        CHECK(strcmp(info.prefix, "backup/") == 0 && !info.has_meta && info.file_count == 2);
        char d2[600];
        snprintf(d2, sizeof d2, "%s/dst2", g_tmp);
        mkdirs(d2);
        CHECK(savezip_extract(zb.data, zb.size, &info, d2, NULL, err, sizeof err));
        snprintf(b, sizeof b, "%s/slot_00/progress.sav", d2);
        CHECK(file_exists(b));
        snprintf(b, sizeof b, "%s/readme.txt", d2);
        CHECK(!file_exists(b));
        buf_free(&zb);
    }
    CHECK(savezip_is_junk("Thumbs.db") && savezip_is_junk("x/a.7Z") && !savezip_is_junk("progress.sav"));
}

// --- WebDAV against the mock server -----------------------------------------

static char *read_text(const char *path) {
    Buffer b = {0};
    if (!file_read_all(path, &b)) return NULL;
    buf_append(&b, "", 0);
    return (char *)b.data;
}

// Returns the JSON object text of the last request with the given method.
static const char *last_req(const char *log, const char *method, char *out, size_t n) {
    char key[64];
    snprintf(key, sizeof key, "\"method\": \"%s\"", method);
    const char *p = NULL, *q = log;
    while ((q = strstr(q, key)) != NULL) p = q++;
    if (!p) return NULL;
    const char *e = strstr(p, "\"body_len\"");
    size_t len = e ? (size_t)(e - p) + 20 : strlen(p);
    if (len >= n) len = n - 1;
    memcpy(out, p, len);
    out[len] = 0;
    return out;
}

static void test_webdav(const char *port, const char *reqlog) {
    Config cfg;
    char json[512];
    snprintf(json, sizeof json,
             "{\"origin\":\"http://127.0.0.1:%s\",\"basepath\":\"JKSV/The Legend of Zelda Tears of the Kingdom\","
             "\"username\":\"u\",\"password\":\"p\"}", port);
    CHECK(config_parse(json, &cfg) == CONFIG_OK);
    dav_set_retries(2, 50);

    // Folder doesn't exist -> created segment by segment.
    DavResult r;
    CHECK(dav_ensure_folder(&cfg, &r));
    Buffer body = {0};
    r = dav_propfind(&cfg, NULL, 0, &body);
    CHECK(dav_ok(&r) && r.http_code == 207);
    buf_free(&body);

    // PUT: Content-Length set, no Expect, no chunked.
    static uint8_t payload[3 * 1024 * 1024 + 17];
    for (size_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i * 31);
    r = dav_put(&cfg, "SW_2026-10-31_12-00-00.zip.part", payload, sizeof payload, NULL);
    CHECK(dav_ok(&r) && r.http_code == 201);
    char *log = read_text(reqlog);
    char req[4096];
    CHECK(log && last_req(log, "PUT", req, sizeof req));
    if (log) {
        char cl[64];
        snprintf(cl, sizeof cl, "\"content-length\": \"%zu\"", sizeof payload);
        CHECK(strstr(req, cl) != NULL);
        CHECK(strstr(req, "\"expect\"") == NULL);
        CHECK(strstr(req, "\"transfer-encoding\"") == NULL);
        CHECK(strstr(req, "\"path\": \"/JKSV/The Legend of Zelda Tears of the Kingdom/SW_2026-10-31_12-00-00.zip.part\"") != NULL);
        free(log);
    }

    // MOVE to the final name.
    r = dav_move(&cfg, "SW_2026-10-31_12-00-00.zip.part", "SW_2026-10-31_12-00-00.zip");
    CHECK(dav_ok(&r));

    // GET returns identical bytes.
    Buffer got = {0};
    r = dav_get(&cfg, "SW_2026-10-31_12-00-00.zip", &got);
    CHECK(dav_ok(&r) && got.size == sizeof payload && memcmp(got.data, payload, sizeof payload) == 0);
    buf_free(&got);

    // Listing shows the file, not the .part.
    r = dav_put(&cfg, "PC_2026-10-31_13-00-00.zip", "x", 1, NULL);
    CHECK(dav_ok(&r));
    r = dav_propfind(&cfg, NULL, 1, &body);
    CHECK(dav_ok(&r));
    DavList l;
    CHECK(propfind_parse((const char *)body.data, &l));
    CHECK(davlist_newest(&l, "SW_", ".zip") && !strcmp(davlist_newest(&l, "SW_", ".zip")->name, "SW_2026-10-31_12-00-00.zip"));
    CHECK(davlist_newest(&l, "PC_", ".zip") != NULL);
    bool has_part = false;
    for (size_t i = 0; i < l.count; i++) has_part |= str_ends_with(l.items[i].name, ".part");
    CHECK(!has_part);
    davlist_free(&l);
    buf_free(&body);

    // Experiment options: chunked + Expect actually reach the wire.
    PutOptions exp = {.send_expect = true, .chunked = true};
    r = dav_put(&cfg, "TEST_chunked.bin", payload, 100000, &exp);
    CHECK(dav_ok(&r));
    log = read_text(reqlog);
    if (log && last_req(log, "PUT", req, sizeof req)) {
        CHECK(strstr(req, "\"transfer-encoding\": \"chunked\"") != NULL);
        CHECK(strstr(req, "\"expect\": \"100-continue\"") != NULL);
    }
    free(log);

    // 500 once -> retried and succeeds.
    r = dav_put(&cfg, "fail500.zip", payload, 1000, NULL);
    CHECK(dav_ok(&r) && r.attempts == 2);

    // Wrong password -> 401, not retried.
    Config bad = cfg;
    str_copy(bad.password, sizeof bad.password, "wrong");
    r = dav_propfind(&bad, NULL, 0, NULL);
    CHECK(r.http_code == 401 && r.attempts == 1);
    CHECK(strstr(dav_explain(&r), "401") != NULL);

    // 404 on missing file.
    r = dav_get(&cfg, "nope.zip", &got);
    CHECK(r.http_code == 404);
    buf_free(&got);

    // Connection refused -> curl error, retried, explained.
    Config dead = cfg;
    str_copy(dead.origin, sizeof dead.origin, "http://127.0.0.1:1");
    r = dav_propfind(&dead, NULL, 0, NULL);
    CHECK(r.curl_code != 0 && r.attempts == 3);
}

static void test_backups(void) {
    char dir[600], p[700];
    snprintf(dir, sizeof dir, "%s/backups", g_tmp);
    mkdirs(dir);
    const char *names[] = {"2026-10-01_10-00-00_before_pull.zip", "2026-10-03_10-00-00_before_pull.zip",
                           "2026-10-02_10-00-00_before_pull.zip", "keep-me.zip", "notes.txt"};
    for (int i = 0; i < 5; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        write_file(p, "x", 1);
    }
    CHECK(backups_prune(dir, 2) == 1);
    snprintf(p, sizeof p, "%s/%s", dir, names[0]);
    CHECK(!file_exists(p));
    for (int i = 1; i < 5; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        CHECK(file_exists(p));
    }
    CHECK(backups_prune(dir, 2) == 0);
    CHECK(backups_prune("/nonexistent-dir-xyz", 2) == -1);
}

static void rm_rf(const char *path) {
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", path);
    if (system(cmd) != 0) fprintf(stderr, "cleanup failed\n");
}

int main(int argc, char **argv) {
    snprintf(g_tmp, sizeof g_tmp, "/tmp/totk-sync-test-%d", (int)getpid());
    if (argc > 2) snprintf(g_tmp, sizeof g_tmp, "%s/totk-sync-test-%d", argv[2], (int)getpid());
    rm_rf(g_tmp);
    mkdirs(g_tmp);

    test_util();
    test_config();
    test_propfind();
    test_meta();
    test_zip();
    test_backups();
    if (argc > 1) {
        dav_global_init();
        char reqlog[600];
        snprintf(reqlog, sizeof reqlog, "%s/requests.json", argc > 2 ? argv[2] : "/tmp");
        test_webdav(argv[1], reqlog);
        dav_global_cleanup();
    } else {
        printf("(webdav tests skipped: no mock server port given)\n");
    }
    rm_rf(g_tmp);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
