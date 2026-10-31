#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

// ---------------------------------------------------------------------------
// Tiny JSON scanner: enough for a flat object of strings/numbers/booleans.
// Nested objects/arrays are skipped. Only top-level keys are matched.
// ---------------------------------------------------------------------------

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

// Parses a JSON string starting at '"'. Writes decoded text to dst (if not NULL).
// Returns pointer after the closing quote, or NULL on error.
static const char *parse_string(const char *p, char *dst, size_t dst_size, bool *truncated) {
    if (*p != '"') return NULL;
    p++;
    size_t o = 0;
    if (truncated) *truncated = false;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char e = *p++;
            switch (e) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    unsigned v = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *p++;
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                        else return NULL;
                    }
                    // Encode as UTF-8 (BMP only; surrogates are not expected in config).
                    char u[3];
                    size_t n;
                    if (v < 0x80) { u[0] = (char)v; n = 1; }
                    else if (v < 0x800) { u[0] = (char)(0xC0 | (v >> 6)); u[1] = (char)(0x80 | (v & 0x3F)); n = 2; }
                    else { u[0] = (char)(0xE0 | (v >> 12)); u[1] = (char)(0x80 | ((v >> 6) & 0x3F)); u[2] = (char)(0x80 | (v & 0x3F)); n = 3; }
                    for (size_t i = 0; i < n; i++) {
                        if (dst && o + 1 < dst_size) dst[o++] = u[i];
                        else if (truncated) *truncated = true;
                    }
                    continue;
                }
                default: return NULL;
            }
        }
        if (dst && o + 1 < dst_size) dst[o++] = c;
        else if (truncated) *truncated = true;
    }
    if (*p != '"') return NULL;
    if (dst && dst_size) dst[o] = 0;
    return p + 1;
}

// Skips any JSON value. Returns pointer after it or NULL.
static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"') return parse_string(p, NULL, 0, NULL);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = parse_string(p, NULL, 0, NULL);
                if (!p) return NULL;
                continue;
            }
            if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') {
                if (--depth == 0) return p + 1;
            }
            p++;
        }
        return NULL;
    }
    // number / true / false / null
    const char *s = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.')) p++;
    return p == s ? NULL : p;
}

// Finds the value of a top-level key. Returns pointer to the value start or NULL.
static const char *find_key(const char *json, const char *key) {
    const char *p = skip_ws(json);
    // Tolerate a UTF-8 BOM.
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p = skip_ws(p + 3);
    if (*p != '{') return NULL;
    p++;
    for (;;) {
        p = skip_ws(p);
        if (*p == '}') return NULL;
        char name[64];
        bool trunc;
        p = parse_string(p, name, sizeof name, &trunc);
        if (!p) return NULL;
        p = skip_ws(p);
        if (*p != ':') return NULL;
        p = skip_ws(p + 1);
        if (!trunc && strcmp(name, key) == 0) return p;
        p = skip_value(p);
        if (!p) return NULL;
        p = skip_ws(p);
        if (*p == ',') p++;
        else if (*p == '}') return NULL;
        else return NULL;
    }
}

static bool json_valid_object(const char *json) {
    const char *p = skip_ws(json);
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p = skip_ws(p + 3);
    if (*p != '{') return false;
    p = skip_value(p);
    return p && *skip_ws(p) == 0;
}

bool json_get_string(const char *json, const char *key, char *dst, size_t dst_size) {
    const char *v = find_key(json, key);
    if (!v || *v != '"') return false;
    bool trunc;
    return parse_string(v, dst, dst_size, &trunc) != NULL && !trunc;
}

bool json_get_int(const char *json, const char *key, int *out) {
    const char *v = find_key(json, key);
    if (!v || !(*v == '-' || isdigit((unsigned char)*v))) return false;
    char *end;
    long n = strtol(v, &end, 10);
    if (end == v) return false;
    *out = (int)n;
    return true;
}

bool json_get_bool(const char *json, const char *key, bool *out) {
    const char *v = find_key(json, key);
    if (!v) return false;
    if (strncmp(v, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(v, "false", 5) == 0) { *out = false; return true; }
    return false;
}

// ---------------------------------------------------------------------------

static void normalize(Config *c) {
    size_t n = strlen(c->origin);
    while (n > 0 && c->origin[n - 1] == '/') c->origin[--n] = 0;

    // Trim slashes around basepath.
    char *b = c->basepath;
    while (*b == '/') b++;
    memmove(c->basepath, b, strlen(b) + 1);
    n = strlen(c->basepath);
    while (n > 0 && c->basepath[n - 1] == '/') c->basepath[--n] = 0;

    if (c->keep_backups < 1) c->keep_backups = 1;
    if (c->keep_backups > 1000) c->keep_backups = 1000;
}

static void defaults(Config *c) {
    memset(c, 0, sizeof *c);
    c->keep_backups = 20;
    c->use_temp_name = true;
}

ConfigResult config_parse(const char *json, Config *out) {
    defaults(out);
    if (!json_valid_object(json)) return CONFIG_PARSE_ERROR;
    if (!json_get_string(json, "origin", out->origin, sizeof out->origin) ||
        !json_get_string(json, "username", out->username, sizeof out->username) ||
        !json_get_string(json, "password", out->password, sizeof out->password))
        return CONFIG_MISSING_FIELDS;
    if (!json_get_string(json, "basepath", out->basepath, sizeof out->basepath))
        snprintf(out->basepath, sizeof out->basepath, "JKSV/%s", TOTK_FOLDER_NAME);
    json_get_int(json, "keep_backups", &out->keep_backups);
    json_get_bool(json, "use_temp_name", &out->use_temp_name);
    json_get_string(json, "cainfo", out->cainfo, sizeof out->cainfo);
    normalize(out);
    if (!out->origin[0] || !out->username[0] || !out->password[0]) return CONFIG_MISSING_FIELDS;
    str_copy(out->source, sizeof out->source, "config.json");
    return CONFIG_OK;
}

ConfigResult config_parse_jksv(const char *json, Config *out) {
    defaults(out);
    if (!json_valid_object(json)) return CONFIG_PARSE_ERROR;
    if (!json_get_string(json, "origin", out->origin, sizeof out->origin) ||
        !json_get_string(json, "username", out->username, sizeof out->username) ||
        !json_get_string(json, "password", out->password, sizeof out->password))
        return CONFIG_MISSING_FIELDS;
    char base[400] = "";
    json_get_string(json, "basepath", base, sizeof base);
    normalize(out);  // trims origin
    // Trim base slashes then append the TotK folder.
    char *b = base;
    while (*b == '/') b++;
    size_t n = strlen(b);
    while (n > 0 && b[n - 1] == '/') b[--n] = 0;
    if (*b)
        snprintf(out->basepath, sizeof out->basepath, "%s/%s", b, TOTK_FOLDER_NAME);
    else
        snprintf(out->basepath, sizeof out->basepath, "%s", TOTK_FOLDER_NAME);
    if (!out->origin[0] || !out->username[0] || !out->password[0]) return CONFIG_MISSING_FIELDS;
    str_copy(out->source, sizeof out->source, "JKSV webdav.json");
    return CONFIG_OK;
}

ConfigResult config_load(const char *primary_path, const char *jksv_path, Config *out) {
    Buffer b = {0};
    if (file_read_all(primary_path, &b) && b.size > 0) {
        buf_append(&b, "", 0);  // NUL-terminate
        ConfigResult r = config_parse((const char *)b.data, out);
        buf_free(&b);
        return r;
    }
    buf_free(&b);
    if (file_read_all(jksv_path, &b) && b.size > 0) {
        buf_append(&b, "", 0);
        ConfigResult r = config_parse_jksv((const char *)b.data, out);
        buf_free(&b);
        return r;
    }
    buf_free(&b);
    defaults(out);
    return CONFIG_NOT_FOUND;
}

const char *config_result_str(ConfigResult r) {
    switch (r) {
        case CONFIG_OK: return "OK";
        case CONFIG_NOT_FOUND: return "config not found";
        case CONFIG_PARSE_ERROR: return "config is not valid JSON";
        case CONFIG_MISSING_FIELDS: return "config lacks origin/username/password";
    }
    return "?";
}
