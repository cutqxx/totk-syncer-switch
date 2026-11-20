#ifndef TS_SYSTEM_NX_H
#define TS_SYSTEM_NX_H

#include <stdbool.h>

#include "account.h"
#include "game_guard.h"

/* Opens pmdmnt and account services. A service that fails to open is not fatal: the probe then reports "cannot check"
   and the account list is empty. */
void system_nx_init(void);
void system_nx_exit(void);

/* console_port_t.find_game_process: pmdmntGetProcessId(TotK) and our own pid (svcGetProcessId). */
void system_nx_find_game_process(void *ctx, game_process_info_t *out);

/* console_port_t.list_accounts_with_save: Account-type TotK saves in the user save space, with ASCII nicknames. */
int system_nx_list_accounts_with_save(void *ctx, account_t *out, int max);

/* console_port_t.set_home_blocked: appletBeginBlockingHomeButton / appletEndBlockingHomeButton. */
void system_nx_set_home_blocked(void *ctx, bool is_blocked);

#endif
