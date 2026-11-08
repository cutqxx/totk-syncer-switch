#include "body.h"

#include <stdlib.h>
#include <string.h>

bool body_append(body_t *body, const void *data, size_t length, int64_t limit)
{
    if (length == 0) {
        return true;
    }
    if (body->size + (int64_t)length > limit) {
        return false;
    }
    size_t new_size = (size_t)body->size + length;
    unsigned char *grown = realloc(body->data, new_size + 1);
    if (grown == NULL) {
        return false;
    }
    memcpy(grown + body->size, data, length);
    grown[new_size] = '\0';
    body->data = grown;
    body->size = (int64_t)new_size;
    return true;
}

bool body_adopt(body_t *body, unsigned char *heap_data, int64_t size)
{
    unsigned char *terminated = realloc(heap_data, (size_t)size + 1);
    if (terminated == NULL) {
        free(heap_data);
        return false;
    }
    terminated[size] = '\0';
    body->data = terminated;
    body->size = size;
    return true;
}

void body_free(body_t *body)
{
    free(body->data);
    body->data = NULL;
    body->size = 0;
}
