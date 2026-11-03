#ifndef TS_BODY_H
#define TS_BODY_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned char *data;
    int64_t size;
} body_t;

typedef enum {
    BODY_OK,
    BODY_ERR_NOT_FOUND,
    BODY_ERR_EMPTY,
    BODY_ERR_TOO_LARGE,
    BODY_ERR_READ,
    BODY_ERR_NO_MEMORY
} body_status_t;

/* On BODY_ERR_TOO_LARGE, out->size holds the real file size (data stays NULL). */
body_status_t body_load(const char *fs_path, int64_t max_size, body_t *out);
void body_free(body_t *body);

void body_status_message(body_status_t status, const char *display_path, int64_t size, int64_t max_size, char *out, size_t out_size);

#endif
