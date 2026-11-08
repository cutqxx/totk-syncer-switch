#ifndef TS_ACCOUNT_PICK_H
#define TS_ACCOUNT_PICK_H

#include "ports.h"

typedef enum {
    PICK_NO_SAVE,   /* no account has a TotK save */
    PICK_CHOSEN,    /* *chosen is set (the only account, or the user's choice) */
    PICK_CANCELLED  /* the user backed out of the list */
} pick_status_t;

/* 0 accounts: PICK_NO_SAVE. 1 account: chosen without asking. Several: ui->choose_account. */
pick_status_t account_pick(const sync_env_t *env, account_t *chosen);

/* Text of the account list screen: one line per account, the selected one marked with ">". */
void account_menu_render(const account_t *list, int count, int selected, char *out, size_t out_size);

#endif
