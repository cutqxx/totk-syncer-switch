#include "body.h"

#include <stdio.h>
#include <stdlib.h>

#include "text_ru.h"

static int64_t file_size(FILE *file)
{
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    return size;
}

static body_status_t read_all(FILE *file, int64_t size, body_t *out)
{
    out->data = malloc((size_t)size);
    if (out->data == NULL) {
        return BODY_ERR_NO_MEMORY;
    }
    if (fread(out->data, 1, (size_t)size, file) != (size_t)size) {
        body_free(out);
        return BODY_ERR_READ;
    }
    out->size = size;
    return BODY_OK;
}

body_status_t body_load(const char *fs_path, int64_t max_size, body_t *out)
{
    out->data = NULL;
    out->size = 0;
    FILE *file = fopen(fs_path, "rb");
    if (file == NULL) {
        return BODY_ERR_NOT_FOUND;
    }
    int64_t size = file_size(file);
    body_status_t status;
    if (size < 0) status = BODY_ERR_READ;
    else if (size == 0) status = BODY_ERR_EMPTY;
    else if (size > max_size) { status = BODY_ERR_TOO_LARGE; out->size = size; }
    else status = read_all(file, size, out);
    fclose(file);
    return status;
}

void body_free(body_t *body)
{
    free(body->data);
    body->data = NULL;
    body->size = 0;
}

void body_status_message(body_status_t status, const char *display_path, int64_t size, int64_t max_size, char *out, size_t out_size)
{
    switch (status) {
    case BODY_ERR_NOT_FOUND: snprintf(out, out_size, TXT_FILE_NOT_FOUND, display_path); break;
    case BODY_ERR_EMPTY: snprintf(out, out_size, TXT_FILE_EMPTY, display_path); break;
    case BODY_ERR_TOO_LARGE: snprintf(out, out_size, TXT_FILE_TOO_LARGE, (long long)size, (long long)max_size, display_path); break;
    case BODY_ERR_NO_MEMORY: snprintf(out, out_size, TXT_FILE_NO_MEMORY, display_path); break;
    default: snprintf(out, out_size, TXT_TEST_ZIP_UNREADABLE, display_path); break;
    }
}
