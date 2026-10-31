// Parsing of WebDAV PROPFIND (Depth: 1) multistatus responses.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    char name[256];    // decoded last path segment (file name)
    bool is_dir;
    time_t modified;   // 0 if unknown
    int64_t size;      // -1 if unknown
} DavEntry;

typedef struct {
    DavEntry *items;
    size_t count;
} DavList;

// Parses the XML body. Namespace prefixes (d:, D:, none) are ignored.
// Returns false on allocation failure or if no <response> elements were found.
bool propfind_parse(const char *xml, DavList *out);
void davlist_free(DavList *l);

// Picks the newest regular file whose name starts with `prefix` and ends with
// `suffix` (by modified time, then by name). Returns NULL if none.
const DavEntry *davlist_newest(const DavList *l, const char *prefix, const char *suffix);
