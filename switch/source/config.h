// Configuration: sd:/config/totk-sync/config.json, with fallback to JKSV's webdav.json.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define TOTK_FOLDER_NAME "The Legend of Zelda Tears of the Kingdom"

typedef struct {
    char origin[256];    // e.g. https://webdav.yandex.com (no trailing slash)
    char basepath[512];  // e.g. JKSV/The Legend of Zelda Tears of the Kingdom (no leading/trailing slash)
    char username[128];
    char password[128];
    char cainfo[256];    // optional CA bundle path; empty = system trust store
    int keep_backups;    // number of SD backups to keep
    bool use_temp_name;  // upload to *.part then MOVE (default true)
    char source[64];     // which file the config came from (for display)
} Config;

typedef enum {
    CONFIG_OK = 0,
    CONFIG_NOT_FOUND,
    CONFIG_PARSE_ERROR,
    CONFIG_MISSING_FIELDS,
} ConfigResult;

// Parses our own config.json text.
ConfigResult config_parse(const char *json, Config *out);
// Parses JKSV's webdav.json text (basepath gets "/<TotK folder>" appended).
ConfigResult config_parse_jksv(const char *json, Config *out);
// Loads from the primary path, falling back to the JKSV path.
ConfigResult config_load(const char *primary_path, const char *jksv_path, Config *out);
const char *config_result_str(ConfigResult r);

// Minimal flat-JSON accessor used by the parsers (exposed for tests).
// Finds a top-level key and copies a string value. Returns false if absent/not a string.
bool json_get_string(const char *json, const char *key, char *dst, size_t dst_size);
// Finds a top-level key with a number value.
bool json_get_int(const char *json, const char *key, int *out);
// Finds a top-level key with true/false value.
bool json_get_bool(const char *json, const char *key, bool *out);
