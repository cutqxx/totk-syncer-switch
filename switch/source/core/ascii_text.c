#include "ascii_text.h"

static char ascii_or_question_mark(unsigned char c)
{
    return (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
}

size_t ascii_copy(const char *text, char *out, size_t out_size)
{
    size_t length = 0;
    for (; text != NULL && text[length] != '\0' && length + 1 < out_size; length++) {
        out[length] = ascii_or_question_mark((unsigned char)text[length]);
    }
    out[length] = '\0';
    return length;
}
