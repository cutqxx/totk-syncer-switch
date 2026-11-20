#ifndef TS_SAVE_VALIDATE_H
#define TS_SAVE_VALIDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zip_reader.h"

#define SAVE_VALIDATE_REASON_SIZE 128

/* What a valid save ZIP will put into the save: junk and .nx_save_meta.bin are not counted. */
typedef struct {
    int files;
    int directories;
    int64_t total_bytes;
} save_zip_stats_t;

/* The "valid save ZIP" rules of the spec, checked in the PC launcher's order (SaveZipStructure, then CRC, then the
   JKSV meta file) with the launcher's reason texts (contracts/save-zip/cases.json). Every file is inflated once to
   check its CRC. On failure reason holds the rule, ASCII. The Switch is stricter on one point: a path byte outside
   printable ASCII is an unsafe path (the PC accepts other Unicode letters). */
bool save_validate_zip(zip_reader_t *zip, save_zip_stats_t *stats, char *reason, size_t reason_size);

#endif
