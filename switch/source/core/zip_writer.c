#include "zip_writer.h"

#include <stdlib.h>
#include <string.h>

#include "miniz.h"

#define NAME_SIZE 300
#define PER_ENTRY_OVERHEAD_BYTES 256
#define ARCHIVE_OVERHEAD_BYTES 1024

struct zip_writer {
    mz_zip_archive archive;
    time_t entry_time;
    size_t reserved_bytes;
    void *heap_block;
    int heap_growths;
};

static void *tracking_alloc(void *opaque, size_t items, size_t size)
{
    zip_writer_t *writer = opaque;
    void *block = malloc(items * size);
    if (writer->heap_block == NULL && items * size == writer->reserved_bytes) {
        writer->heap_block = block;
    }
    return block;
}

static void *tracking_realloc(void *opaque, void *address, size_t items, size_t size)
{
    zip_writer_t *writer = opaque;
    bool is_heap_growth = address != NULL && address == writer->heap_block;
    void *block = realloc(address, items * size);
    if (is_heap_growth) {
        writer->heap_growths++;
        writer->heap_block = block;
    }
    return block;
}

static void plain_free(void *opaque, void *address)
{
    (void)opaque;
    free(address);
}

/* Worst case for incompressible data at MZ_BEST_SPEED: miniz emits deflate stored blocks of up to 64 KiB
   (5 bytes each, about 0.008%), covered by total / 1000. Each entry adds a local header, a central directory
   record and one final deflate block: 30 + 46 + 10 bytes plus two copies of the name, covered by
   PER_ENTRY_OVERHEAD_BYTES for names up to about 80 characters. ARCHIVE_OVERHEAD_BYTES covers the
   end-of-central-directory record and slack. */
static size_t reserved_bytes_for(int entry_count, size_t expected_bytes)
{
    return expected_bytes + (size_t)entry_count * PER_ENTRY_OVERHEAD_BYTES + expected_bytes / 1000 +
           ARCHIVE_OVERHEAD_BYTES;
}

static time_t to_time_t(const struct tm *local_time)
{
    struct tm copy = *local_time;
    copy.tm_isdst = -1;
    return mktime(&copy);
}

zip_writer_t *zip_writer_open(const struct tm *entry_time, int entry_count, size_t expected_bytes)
{
    zip_writer_t *writer = calloc(1, sizeof *writer);
    if (writer == NULL) {
        return NULL;
    }
    writer->entry_time = to_time_t(entry_time);
    writer->reserved_bytes = reserved_bytes_for(entry_count, expected_bytes);
    writer->archive.m_pAlloc = tracking_alloc;
    writer->archive.m_pRealloc = tracking_realloc;
    writer->archive.m_pFree = plain_free;
    writer->archive.m_pAlloc_opaque = writer;
    if (!mz_zip_writer_init_heap(&writer->archive, 0, writer->reserved_bytes)) {
        free(writer);
        return NULL;
    }
    return writer;
}

static bool add_entry(zip_writer_t *writer, const char *name, const void *data, size_t size)
{
    return mz_zip_writer_add_mem_ex_v2(&writer->archive, name, data, size, NULL, 0, MZ_BEST_SPEED, 0, 0,
                                       &writer->entry_time, NULL, 0, NULL, 0) != MZ_FALSE;
}

bool zip_writer_add_directory(zip_writer_t *writer, const char *name)
{
    char directory_name[NAME_SIZE];
    size_t length = strlen(name);
    if (length + 2 > sizeof directory_name) {
        return false;
    }
    memcpy(directory_name, name, length);
    if (length == 0 || name[length - 1] != '/') {
        directory_name[length++] = '/';
    }
    directory_name[length] = '\0';
    return add_entry(writer, directory_name, NULL, 0);
}

bool zip_writer_add_file(zip_writer_t *writer, const char *name, const void *data, size_t size)
{
    return add_entry(writer, name, data, size);
}

int zip_writer_heap_growths(const zip_writer_t *writer)
{
    return writer->heap_growths;
}

bool zip_writer_finish(zip_writer_t *writer, body_t *out)
{
    void *buffer = NULL;
    size_t size = 0;
    bool is_finalized = mz_zip_writer_finalize_heap_archive(&writer->archive, &buffer, &size) != MZ_FALSE;
    mz_zip_writer_end(&writer->archive);
    free(writer);
    if (!is_finalized) {
        return false;
    }
    return body_adopt(out, buffer, (int64_t)size);
}

void zip_writer_abort(zip_writer_t *writer)
{
    if (writer == NULL) {
        return;
    }
    mz_zip_writer_end(&writer->archive);
    free(writer);
}
