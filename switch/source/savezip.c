#include "savezip.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "miniz.h"

#define ZIP_LEVEL 6
#define WRITE_CHUNK (256 * 1024)
#define MAX_DEPTH 8

static void set_err(char *err, size_t n, const char *fmt, ...) {
    if (!err || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

bool savezip_is_junk(const char *name) {
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (strcmp(base, SAVE_META_NAME) == 0) return true;
    if (strcasecmp(base, "desktop.ini") == 0 || strcasecmp(base, "Thumbs.db") == 0) return true;
    size_t n = strlen(base);
    static const char *exts[] = {".7z", ".zip", ".rar"};
    for (size_t i = 0; i < 3; i++) {
        size_t e = strlen(exts[i]);
        if (n >= e && strcasecmp(base + n - e, exts[i]) == 0) return true;
    }
    return false;
}

static void join(char *dst, size_t n, const char *a, const char *b) {
    size_t la = strlen(a);
    if (la > 0 && a[la - 1] == '/')
        snprintf(dst, n, "%s%s", a, b);
    else
        snprintf(dst, n, "%s/%s", a, b);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

// Recursively adds `dir_path` (archive prefix `rel`, "" or "x/") to the zip.
static bool add_dir(mz_zip_archive *zip, const char *dir_path, const char *rel, int depth, time_t *now,
                    char *err, size_t err_size) {
    if (depth > MAX_DEPTH) {
        set_err(err, err_size, "directory tree too deep");
        return false;
    }
    DIR *d = opendir(dir_path);
    if (!d) {
        set_err(err, err_size, "cannot open %s", dir_path);
        return false;
    }
    // Collect + sort names for a deterministic archive.
    char **names = NULL;
    size_t count = 0, cap = 0;
    struct dirent *de;
    bool ok = true;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 32;
            char **nn = realloc(names, cap * sizeof *nn);
            if (!nn) { ok = false; break; }
            names = nn;
        }
        names[count] = strdup(de->d_name);
        if (!names[count]) { ok = false; break; }
        count++;
    }
    closedir(d);
    if (!ok) set_err(err, err_size, "out of memory");
    if (ok) qsort(names, count, sizeof *names, cmp_str);

    for (size_t i = 0; ok && i < count; i++) {
        char full[768], arch[512];
        join(full, sizeof full, dir_path, names[i]);
        snprintf(arch, sizeof arch, "%s%s", rel, names[i]);
        struct stat st;
        if (stat(full, &st) != 0) {
            set_err(err, err_size, "cannot stat %s", full);
            ok = false;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            char arch_dir[520];
            snprintf(arch_dir, sizeof arch_dir, "%s/", arch);
            if (!mz_zip_writer_add_mem_ex_v2(zip, arch_dir, NULL, 0, NULL, 0, 0, 0, 0, now, NULL, 0, NULL, 0)) {
                set_err(err, err_size, "zip: cannot add %s", arch_dir);
                ok = false;
                break;
            }
            ok = add_dir(zip, full, arch_dir, depth + 1, now, err, err_size);
        } else {
            if (savezip_is_junk(names[i])) continue;
            Buffer b = {0};
            if (!file_read_all(full, &b)) {
                set_err(err, err_size, "cannot read %s", full);
                buf_free(&b);
                ok = false;
                break;
            }
            ok = mz_zip_writer_add_mem_ex_v2(zip, arch, b.data ? b.data : (const void *)"", b.size, NULL, 0,
                                             ZIP_LEVEL, 0, 0, now, NULL, 0, NULL, 0);
            buf_free(&b);
            if (!ok) set_err(err, err_size, "zip: cannot add %s", arch);
        }
    }
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
    return ok;
}

bool savezip_build(const char *root_dir, const uint8_t *meta, Buffer *out, char *err, size_t err_size) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_heap(&zip, 0, 8 * 1024 * 1024)) {
        set_err(err, err_size, "zip: init failed");
        return false;
    }
    time_t now = time(NULL);
    bool ok = true;
    if (meta) {
        ok = mz_zip_writer_add_mem_ex_v2(&zip, SAVE_META_NAME, meta, SAVE_META_SIZE, NULL, 0, ZIP_LEVEL, 0, 0,
                                         &now, NULL, 0, NULL, 0);
        if (!ok) set_err(err, err_size, "zip: cannot add meta");
    }
    if (ok) ok = add_dir(&zip, root_dir, "", 0, &now, err, err_size);

    void *data = NULL;
    size_t size = 0;
    if (ok && !mz_zip_writer_finalize_heap_archive(&zip, &data, &size)) {
        set_err(err, err_size, "zip: finalize failed");
        ok = false;
    }
    mz_zip_writer_end(&zip);
    if (!ok) {
        free(data);
        return false;
    }
    buf_free(out);
    out->data = data;
    out->size = out->cap = size;
    return true;
}

// Rejects absolute paths, "..", backslashes and drive/device prefixes.
static bool safe_name(const char *name) {
    if (!name[0] || name[0] == '/' || strchr(name, '\\') || strchr(name, ':')) return false;
    const char *p = name;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        if ((n == 2 && p[0] == '.' && p[1] == '.') || (n == 1 && p[0] == '.')) return false;
        if (!slash) break;
        p = slash + 1;
    }
    return true;
}

bool savezip_validate(const uint8_t *data, size_t size, SaveZipInfo *info, char *err, size_t err_size) {
    memset(info, 0, sizeof *info);
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) {
        set_err(err, err_size, "not a ZIP file");
        return false;
    }
    bool ok = false;
    mz_uint n = mz_zip_reader_get_num_files(&zip);

    // Integrity: every entry decompresses and matches its CRC.
    if (!mz_zip_validate_archive(&zip, 0)) {
        set_err(err, err_size, "ZIP is damaged (CRC/structure check failed)");
        goto done;
    }

    // Locate option.sav with the shortest path -> save root prefix.
    int best_depth = 1 << 30;
    bool found_option = false;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) {
            set_err(err, err_size, "cannot read ZIP entry %u", i);
            goto done;
        }
        if (!safe_name(st.m_filename)) {
            set_err(err, err_size, "unsafe path in ZIP: %.100s", st.m_filename);
            goto done;
        }
        const char *base = strrchr(st.m_filename, '/');
        base = base ? base + 1 : st.m_filename;
        if (!st.m_is_directory && strcmp(base, "option.sav") == 0) {
            int depth = 0;
            for (const char *c = st.m_filename; *c; c++) depth += (*c == '/');
            if (depth < best_depth) {
                best_depth = depth;
                size_t plen = (size_t)(base - st.m_filename);
                if (plen >= sizeof info->prefix) continue;
                memcpy(info->prefix, st.m_filename, plen);
                info->prefix[plen] = 0;
                found_option = true;
            }
        }
    }
    if (!found_option) {
        set_err(err, err_size, "ZIP has no option.sav - not a TotK save");
        goto done;
    }
    char want[256];
    snprintf(want, sizeof want, "%sslot_00/progress.sav", info->prefix);
    if (mz_zip_reader_locate_file(&zip, want, NULL, MZ_ZIP_FLAG_CASE_SENSITIVE) < 0) {
        set_err(err, err_size, "ZIP has no slot_00/progress.sav - not a TotK save");
        goto done;
    }

    // Optional meta (at the save root or the archive root).
    for (int pass = 0; pass < 2 && !info->has_meta; pass++) {
        char mp[256];
        snprintf(mp, sizeof mp, "%s%s", pass == 0 ? info->prefix : "", SAVE_META_NAME);
        int idx = mz_zip_reader_locate_file(&zip, mp, NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
        if (idx < 0) continue;
        size_t msize = 0;
        void *m = mz_zip_reader_extract_to_heap(&zip, (mz_uint)idx, &msize, 0);
        if (!m) {
            set_err(err, err_size, "cannot read meta file");
            goto done;
        }
        SaveMeta meta;
        bool decoded = savemeta_decode(m, msize, &meta);
        free(m);
        if (!decoded) {
            set_err(err, err_size, "meta file is not a JKSV meta");
            goto done;
        }
        if (meta.application_id != TOTK_TITLE_ID) {
            set_err(err, err_size, "ZIP belongs to another game (title %016llX)",
                    (unsigned long long)meta.application_id);
            goto done;
        }
        info->has_meta = true;
        info->meta = meta;
    }

    // Count files that will be written.
    size_t plen = strlen(info->prefix);
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        mz_zip_reader_file_stat(&zip, i, &st);
        if (st.m_is_directory || strncmp(st.m_filename, info->prefix, plen) != 0) continue;
        if (savezip_is_junk(st.m_filename)) continue;
        info->file_count++;
        info->total_size += st.m_uncomp_size;
    }
    ok = true;
done:
    mz_zip_reader_end(&zip);
    return ok;
}

static bool do_commit(const CommitHooks *h) {
    return !h || !h->commit || h->commit(h->ctx);
}

// Creates every parent directory of `path` below `root`.
static bool ensure_parents(const char *root, const char *rel, const CommitHooks *hooks) {
    char sub[512];
    const char *slash = strrchr(rel, '/');
    if (!slash) return true;
    size_t n = (size_t)(slash - rel);
    if (n >= sizeof sub) return false;
    memcpy(sub, rel, n);
    sub[n] = 0;
    char full[768];
    join(full, sizeof full, root, sub);
    struct stat st;
    if (stat(full, &st) == 0) return S_ISDIR(st.st_mode);
    if (!mkdirs(full)) return false;
    return do_commit(hooks);
}

bool savezip_extract(const uint8_t *data, size_t size, const SaveZipInfo *info, const char *dest_root,
                     const CommitHooks *hooks, char *err, size_t err_size) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) {
        set_err(err, err_size, "not a ZIP file");
        return false;
    }
    bool ok = true;
    size_t plen = strlen(info->prefix);
    int64_t journal_used = 0;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; ok && i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) {
            set_err(err, err_size, "cannot read ZIP entry %u", i);
            ok = false;
            break;
        }
        if (strncmp(st.m_filename, info->prefix, plen) != 0) continue;
        const char *rel = st.m_filename + plen;
        if (!*rel) continue;
        char full[768];
        join(full, sizeof full, dest_root, rel);

        if (st.m_is_directory) {
            size_t l = strlen(full);
            while (l > 0 && full[l - 1] == '/') full[--l] = 0;
            struct stat sst;
            if (stat(full, &sst) == 0) continue;
            if (!mkdirs(full) || !do_commit(hooks)) {
                set_err(err, err_size, "cannot create %s", rel);
                ok = false;
            }
            continue;
        }
        if (savezip_is_junk(rel)) continue;
        if (!ensure_parents(dest_root, rel, hooks)) {
            set_err(err, err_size, "cannot create folder for %s", rel);
            ok = false;
            break;
        }

        size_t fsize = 0;
        uint8_t *buf = mz_zip_reader_extract_to_heap(&zip, i, &fsize, 0);
        if (!buf && st.m_uncomp_size > 0) {
            set_err(err, err_size, "cannot unpack %s", rel);
            ok = false;
            break;
        }
        FILE *f = fopen(full, "wb");
        if (!f) {
            set_err(err, err_size, "cannot create %s", rel);
            free(buf);
            ok = false;
            break;
        }
        size_t off = 0;
        while (ok && off < fsize) {
            size_t chunk = fsize - off < WRITE_CHUNK ? fsize - off : WRITE_CHUNK;
            // Commit before the save's journal would overflow (as JKSV does).
            if (hooks && hooks->commit && hooks->journal_size > 0 &&
                journal_used + (int64_t)chunk >= hooks->journal_size) {
                fclose(f);
                f = NULL;
                if (!do_commit(hooks)) {
                    set_err(err, err_size, "commit failed while writing %s", rel);
                    ok = false;
                    break;
                }
                journal_used = 0;
                f = fopen(full, "r+b");
                if (!f || fseek(f, (long)off, SEEK_SET) != 0) {
                    set_err(err, err_size, "cannot reopen %s", rel);
                    ok = false;
                    break;
                }
            }
            if (fwrite(buf + off, 1, chunk, f) != chunk) {
                set_err(err, err_size, "write failed: %s (save full?)", rel);
                ok = false;
                break;
            }
            off += chunk;
            journal_used += (int64_t)chunk;
        }
        if (f && fclose(f) != 0 && ok) {
            set_err(err, err_size, "close failed: %s", rel);
            ok = false;
        }
        free(buf);
        if (ok && !do_commit(hooks)) {
            set_err(err, err_size, "commit failed after %s", rel);
            ok = false;
        }
        if (ok) journal_used = 0;
    }
    mz_zip_reader_end(&zip);
    if (ok && !do_commit(hooks)) {
        set_err(err, err_size, "final commit failed");
        ok = false;
    }
    return ok;
}
