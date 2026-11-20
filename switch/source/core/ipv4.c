#include "ipv4.h"

#include <stddef.h>

#define OCTET_COUNT 4
#define OCTET_MAX 255

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool parse_octet(const char **cursor, uint8_t *out)
{
    const char *p = *cursor;
    if (!is_digit(*p) || (*p == '0' && is_digit(p[1]))) {
        return false;
    }
    int value = 0;
    while (is_digit(*p)) {
        value = value * 10 + (*p - '0');
        if (value > OCTET_MAX) {
            return false;
        }
        p++;
    }
    *out = (uint8_t)value;
    *cursor = p;
    return true;
}

bool ipv4_parse(const char *text, uint8_t out[4])
{
    const char *cursor = text;
    uint8_t octets[OCTET_COUNT];
    for (int i = 0; i < OCTET_COUNT; i++) {
        if (i > 0 && *cursor++ != '.') {
            return false;
        }
        if (!parse_octet(&cursor, &octets[i])) {
            return false;
        }
    }
    if (*cursor != '\0') {
        return false;
    }
    for (int i = 0; i < OCTET_COUNT; i++) {
        out[i] = octets[i];
    }
    return true;
}

bool ipv4_is_private(const uint8_t ip[4])
{
    if (ip[0] == 10) {
        return true;
    }
    if (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) {
        return true;
    }
    return ip[0] == 192 && ip[1] == 168;
}
