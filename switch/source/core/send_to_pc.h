#ifndef TS_SEND_TO_PC_H
#define TS_SEND_TO_PC_H

#include <stddef.h>
#include <stdint.h>

#include "api_put.h"
#include "config.h"
#include "ports.h"
#include "save_pack.h"

typedef enum {
    SEND_OK,                 /* the PC replaced its save; backup is set */
    SEND_GAME_RUNNING,       /* TotK runs or is suspended on the Switch: nothing mounted, nothing sent */
    SEND_GAME_CHECK_FAILED,  /* the process check itself failed: handled like a running game */
    SEND_NO_SAVE_ON_CONSOLE,
    SEND_CANCELLED,          /* the user left the account list */
    SEND_MOUNT_FAILED,
    SEND_PACK_FAILED,        /* too large, too many entries, bad name, unreadable file, out of memory: nothing sent */
    SEND_NETWORK_FAILED,     /* no usable answer from the PC */
    SEND_PC_REFUSED          /* the PC answered, but not with success */
} send_kind_t;

typedef struct {
    send_kind_t kind;
    char message[TS_LINE_SIZE]; /* what the user reads: the "SENT TO PC" line or the error; ASCII, no token */
    char backup[API_PUT_BACKUP_SIZE];
} send_result_t;

/* The "A - Send to PC" scenario. Steps are shown with ui->show_line; the console port only looks and reads.
   Order: check the game -> pick the account -> mount read-only -> pack -> unmount (always, before any network)
   -> PUT /save (retried once only when the connection could not be opened) -> result.
   max_save_bytes is the limit on summed file sizes (TS_MAX_SAVE_BYTES in the app). */
void send_to_pc(const sync_env_t *env, const config_t *cfg, int64_t max_save_bytes, send_result_t *out);

/* User text for a classified PUT answer (the success line included). The config token is removed from it. */
void send_describe_put(const api_put_result_t *put, const config_t *cfg, char *out, size_t out_size);

/* User text for a failed pack. */
void send_describe_pack_failure(pack_status_t status, const pack_stats_t *stats, int64_t max_save_bytes, char *out, size_t out_size);

/* One log.txt line about a finished upload: ZIP size, unpacked size, compression ratio, time and speed. No bodies. */
void send_format_transfer_note(const pack_stats_t *stats, const http_response_t *resp, char *out, size_t out_size);

#endif
