#ifndef TS_ZIP_WRITER_H
#define TS_ZIP_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "body.h"

/* Builds a ZIP in memory (miniz, fastest deflate). Every entry gets the same timestamp, so equal input gives equal output. */
typedef struct zip_writer zip_writer_t;

/* entry_count and expected_bytes (total uncompressed size) size the buffer up front so it is not regrown even for
   incompressible data. NULL when memory runs out. */
zip_writer_t *zip_writer_open(const struct tm *entry_time, int entry_count, size_t expected_bytes);

/* How many times the output buffer had to be enlarged after the initial allocation. */
int zip_writer_heap_growths(const zip_writer_t *writer);

/* name uses '/' as separator; a trailing '/' is added when missing. */
bool zip_writer_add_directory(zip_writer_t *writer, const char *name);
bool zip_writer_add_file(zip_writer_t *writer, const char *name, const void *data, size_t size);

/* Finalizes the archive into *out (empty body expected) and frees the writer. On failure the writer is freed too. */
bool zip_writer_finish(zip_writer_t *writer, body_t *out);

/* Frees an unfinished writer. */
void zip_writer_abort(zip_writer_t *writer);

#endif
