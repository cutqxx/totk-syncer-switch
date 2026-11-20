#ifndef TS_ASCII_TEXT_H
#define TS_ASCII_TEXT_H

#include <stddef.h>

/* Copies text into out, truncating to fit and replacing every byte outside 0x20-0x7E with '?': the console font
   has only ASCII. NULL text gives an empty string. Returns the number of bytes written, without the NUL.
   out_size must be at least 1. */
size_t ascii_copy(const char *text, char *out, size_t out_size);

#endif
