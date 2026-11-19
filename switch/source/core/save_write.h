#ifndef TS_SAVE_WRITE_H
#define TS_SAVE_WRITE_H

#include <stdbool.h>
#include <stdint.h>

#include "file_ops.h"
#include "ports.h"
#include "zip_reader.h"

/* Uncommitted bytes allowed when the journal size is unknown: small, so a commit comes often. */
#define SAVE_WRITE_FALLBACK_BUDGET (1LL * 1024 * 1024)

typedef struct {
    bool is_ok;
    bool is_changed;            /* a remove, mkdir, write or commit went through: the save may differ from before */
    int commits;                /* successful commits */
    char reason[TS_LINE_SIZE];  /* what failed: the step, the path inside the save and why; ASCII */
} save_write_result_t;

/* Uncommitted bytes allowed for this journal: three quarters of it, or SAVE_WRITE_FALLBACK_BUDGET when unknown (<= 0). */
int64_t save_write_budget(int64_t journal_bytes);

/* Makes the save mounted read-write at root hold exactly what the (already validated) zip holds: everything under root
   is removed, then every directory (empty ones too) and every file is written, junk and .nx_save_meta.bin skipped.
   Writes go through ops; console->commit_save commits. Uncommitted bytes never exceed save_write_budget(journal_bytes):
   a file is closed, committed and reopened for appending when needed, and when the whole save is larger than the
   budget a commit also follows every file (as JKSV does). The last commit follows the last write; is_ok only when it
   passed. Every file is closed before every commit. */
void save_write_zip(const file_ops_t *ops, const console_port_t *console, const char *root, zip_reader_t *zip,
                    int64_t journal_bytes, save_write_result_t *out);

#endif
