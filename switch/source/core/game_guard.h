#ifndef TS_GAME_GUARD_H
#define TS_GAME_GUARD_H

#include <stdbool.h>
#include <stdint.h>

/* What the console probe (pmdmnt) learned about TotK. */
typedef struct {
    bool is_probe_ok;     /* false: the process list could not be queried; error_code says why */
    bool is_found;        /* a TotK process exists (running or suspended in applet mode) */
    uint64_t pid;         /* valid when is_found */
    uint64_t self_pid;    /* the pid of this app */
    uint32_t error_code;  /* valid when !is_probe_ok */
} game_process_info_t;

typedef enum {
    GAME_CLEAR,   /* TotK is not running: the save may be read */
    GAME_RUNNING, /* TotK is running or suspended: do not touch the save */
    GAME_UNKNOWN  /* cannot tell: treated like running, the save is not touched */
} game_state_t;

/* Under a title override this app runs inside the TotK process, so a process with our own pid does not count. */
game_state_t game_guard_check(const game_process_info_t *info);

#endif
