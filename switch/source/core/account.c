#include "account.h"

#include <stdio.h>

#include "ascii_text.h"

void account_make(account_t *out, const uint64_t uid[2], const char *utf8_nickname, int number)
{
    out->uid[0] = uid[0];
    out->uid[1] = uid[1];
    if (ascii_copy(utf8_nickname, out->name, sizeof out->name) == 0) {
        snprintf(out->name, sizeof out->name, "Account %d", number);
    }
}
