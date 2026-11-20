#ifndef TS_ACCOUNT_H
#define TS_ACCOUNT_H

#include <stddef.h>
#include <stdint.h>

#define ACCOUNT_MAX 8
#define ACCOUNT_NAME_SIZE 33 /* 32 shown ASCII characters + NUL */

typedef struct {
    uint64_t uid[2];                 /* the console's AccountUid, opaque to the core */
    char name[ACCOUNT_NAME_SIZE];    /* 7-bit ASCII, safe for the console font */
} account_t;

/* The nickname is UTF-8 (possibly Cyrillic): every byte outside printable ASCII becomes '?'.
   An empty or missing nickname becomes "Account <number>" (number is 1-based). */
void account_make(account_t *out, const uint64_t uid[2], const char *utf8_nickname, int number);

#endif
