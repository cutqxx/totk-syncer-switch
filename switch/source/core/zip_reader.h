#ifndef TS_ZIP_READER_H
#define TS_ZIP_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ts_limits.h"

/* Reads a ZIP that lies in memory (miniz). The bytes are not copied: they must outlive the reader. */
typedef struct zip_reader zip_reader_t;

typedef struct {
    char path[TS_PATH_SIZE];   /* the stored name without the trailing '/' of a directory */
    bool is_name_too_long;     /* the stored name did not fit into path: never safe to write */
    bool is_directory;
    bool is_symlink;           /* unix symlink mode or the windows reparse-point attribute */
    bool is_supported;         /* not encrypted, and a compression method miniz can inflate */
    int64_t size;              /* uncompressed size declared by the archive */
    int64_t packed_size;
} zip_entry_t;

/* NULL when the bytes are not a readable ZIP or memory runs out. */
zip_reader_t *zip_reader_open(const void *data, size_t size);
void zip_reader_close(zip_reader_t *reader);

int zip_reader_count(const zip_reader_t *reader);

/* Fills *out for the entry at index (0 .. count-1). */
void zip_reader_entry(zip_reader_t *reader, int index, zip_entry_t *out);

/* Inflates a file entry into a malloc'd buffer of exactly the declared size, checking the CRC.
   An empty file gives a 1-byte buffer and *size 0. False (nothing allocated) on any mismatch or out of memory. */
bool zip_reader_extract(zip_reader_t *reader, int index, unsigned char **data, size_t *size);

#endif
