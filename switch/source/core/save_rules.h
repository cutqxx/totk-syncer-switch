#ifndef TS_SAVE_RULES_H
#define TS_SAVE_RULES_H

#include <stdbool.h>

/* Same junk list and matching as the PC launcher (JunkFilter): by file name, case-insensitive. */
bool save_rules_is_meta(const char *rel_path);
bool save_rules_is_junk(const char *rel_path);

/* One path segment the PC launcher accepts: printable ASCII, none of < > : " \ | ? *, not "." or "..",
   no trailing dot or space, not a Windows device name (also with an extension or spaces before it). */
bool save_rules_is_safe_segment(const char *segment);

/* Slash-separated relative path whose every segment is safe. */
bool save_rules_is_safe_path(const char *rel_path);

#endif
