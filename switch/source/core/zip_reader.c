#include "zip_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

#define UNIX_TYPE_MASK 0xF000u
#define UNIX_SYMLINK_TYPE 0xA000u
#define WINDOWS_REPARSE_POINT 0x400u
#define STORED_NAME_SIZE (TS_PATH_SIZE + 2)

struct zip_reader {
    mz_zip_archive archive;
};

zip_reader_t *zip_reader_open(const void *data, size_t size)
{
    zip_reader_t *reader = calloc(1, sizeof *reader);
    if (reader == NULL || data == NULL) {
        free(reader);
        return NULL;
    }
    if (!mz_zip_reader_init_mem(&reader->archive, data, size, 0)) {
        free(reader);
        return NULL;
    }
    return reader;
}

void zip_reader_close(zip_reader_t *reader)
{
    if (reader != NULL) {
        mz_zip_reader_end(&reader->archive);
        free(reader);
    }
}

int zip_reader_count(const zip_reader_t *reader)
{
    return (int)mz_zip_reader_get_num_files((mz_zip_archive *)&reader->archive);
}

static void store_path(const char *stored, bool is_directory, zip_entry_t *out)
{
    size_t length = strlen(stored);
    if (is_directory && length > 0 && stored[length - 1] == '/') {
        length--;
    }
    out->is_name_too_long = length >= sizeof out->path;
    if (out->is_name_too_long) {
        length = sizeof out->path - 1;
    }
    memcpy(out->path, stored, length);
    out->path[length] = '\0';
}

void zip_reader_entry(zip_reader_t *reader, int index, zip_entry_t *out)
{
    mz_zip_archive_file_stat stat;
    memset(out, 0, sizeof *out);
    if (!mz_zip_reader_file_stat(&reader->archive, (mz_uint)index, &stat)) {
        out->is_name_too_long = true;
        return;
    }
    out->is_directory = stat.m_is_directory != 0;
    out->is_symlink = ((stat.m_external_attr >> 16) & UNIX_TYPE_MASK) == UNIX_SYMLINK_TYPE
                      || (stat.m_external_attr & WINDOWS_REPARSE_POINT) != 0;
    out->is_supported = stat.m_is_supported != 0 && stat.m_is_encrypted == 0;
    out->size = (int64_t)stat.m_uncomp_size;
    out->packed_size = (int64_t)stat.m_comp_size;
    store_path(stat.m_filename, out->is_directory, out);
}

bool zip_reader_extract(zip_reader_t *reader, int index, unsigned char **data, size_t *size)
{
    mz_zip_archive_file_stat stat;
    *data = NULL;
    *size = 0;
    if (!mz_zip_reader_file_stat(&reader->archive, (mz_uint)index, &stat) || stat.m_is_directory) {
        return false;
    }
    if (stat.m_uncomp_size == 0) {
        *data = malloc(1);
        return *data != NULL && stat.m_crc32 == MZ_CRC32_INIT;
    }
    size_t extracted_size = 0;
    void *extracted = mz_zip_reader_extract_to_heap(&reader->archive, (mz_uint)index, &extracted_size, 0);
    if (extracted == NULL || extracted_size != (size_t)stat.m_uncomp_size) {
        free(extracted);
        return false;
    }
    /* The archive uses miniz's default allocator (malloc), so the caller releases the buffer with free(). */
    *data = extracted;
    *size = extracted_size;
    return true;
}
