#include "save_rules.h"

#include <string.h>

static const char *const JUNK_NAMES[] = { ".nx_save_meta.bin", "desktop.ini", "thumbs.db" };
static const char *const JUNK_EXTENSIONS[] = { ".7z", ".zip", ".rar" };
static const char *const DEVICE_NAMES[] = {
    "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};
#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))
#define META_NAME_INDEX 0
#define FORBIDDEN_CHARACTERS "<>:\"\\|?*"

static char lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool equals_ignoring_case(const char *left, size_t left_length, const char *right)
{
    if (left_length != strlen(right)) {
        return false;
    }
    for (size_t i = 0; i < left_length; i++) {
        if (lower_ascii(left[i]) != lower_ascii(right[i])) {
            return false;
        }
    }
    return true;
}

static const char *file_name_of(const char *rel_path)
{
    const char *slash = strrchr(rel_path, '/');
    const char *backslash = strrchr(rel_path, '\\');
    const char *last = slash > backslash ? slash : backslash;
    return last == NULL ? rel_path : last + 1;
}

bool save_rules_is_meta(const char *rel_path)
{
    const char *name = file_name_of(rel_path);
    return equals_ignoring_case(name, strlen(name), JUNK_NAMES[META_NAME_INDEX]);
}

static bool ends_with_ignoring_case(const char *name, const char *suffix)
{
    size_t name_length = strlen(name);
    size_t suffix_length = strlen(suffix);
    return name_length >= suffix_length && equals_ignoring_case(name + name_length - suffix_length, suffix_length, suffix);
}

bool save_rules_is_junk(const char *rel_path)
{
    const char *name = file_name_of(rel_path);
    for (size_t i = 0; i < COUNT_OF(JUNK_NAMES); i++) {
        if (equals_ignoring_case(name, strlen(name), JUNK_NAMES[i])) {
            return true;
        }
    }
    for (size_t i = 0; i < COUNT_OF(JUNK_EXTENSIONS); i++) {
        if (ends_with_ignoring_case(name, JUNK_EXTENSIONS[i])) {
            return true;
        }
    }
    return false;
}

static bool is_allowed_character(unsigned char c)
{
    return c >= 0x20 && c <= 0x7E && strchr(FORBIDDEN_CHARACTERS, c) == NULL;
}

static bool is_device_name(const char *segment)
{
    size_t base_length = strcspn(segment, ".");
    while (base_length > 0 && segment[base_length - 1] == ' ') {
        base_length--;
    }
    for (size_t i = 0; i < COUNT_OF(DEVICE_NAMES); i++) {
        if (equals_ignoring_case(segment, base_length, DEVICE_NAMES[i])) {
            return true;
        }
    }
    return false;
}

bool save_rules_is_safe_segment(const char *segment)
{
    size_t length = strlen(segment);
    if (length == 0 || strcmp(segment, ".") == 0 || strcmp(segment, "..") == 0) {
        return false;
    }
    if (segment[length - 1] == '.' || segment[length - 1] == ' ') {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (!is_allowed_character((unsigned char)segment[i])) {
            return false;
        }
    }
    return !is_device_name(segment);
}

bool save_rules_is_safe_path(const char *rel_path)
{
    char segment[256];
    const char *start = rel_path;
    for (;;) {
        size_t length = strcspn(start, "/");
        if (length >= sizeof segment) {
            return false;
        }
        memcpy(segment, start, length);
        segment[length] = '\0';
        if (!save_rules_is_safe_segment(segment)) {
            return false;
        }
        if (start[length] == '\0') {
            return true;
        }
        start += length + 1;
    }
}
