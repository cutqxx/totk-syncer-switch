#ifndef TS_BODY_H
#define TS_BODY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* data is NULL while empty; otherwise it holds size bytes followed by one extra NUL byte. */
typedef struct {
    unsigned char *data;
    int64_t size;
} body_t;

/* Returns false (body unchanged) when the result would exceed limit or memory runs out. */
bool body_append(body_t *body, const void *data, size_t length, int64_t limit);
/* Takes over a malloc'd buffer of size bytes (the body must be empty); on success the buffer is grown by the trailing NUL.
   On failure (memory) the buffer is freed and the body stays empty. */
bool body_adopt(body_t *body, unsigned char *heap_data, int64_t size);
void body_free(body_t *body);

#endif
