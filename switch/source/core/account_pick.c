#include "account_pick.h"

#include <stdio.h>
#include <string.h>

#include "text.h"

static pick_status_t choose_from_several(const sync_env_t *env, const char *title, const account_t *list, int count,
                                         account_t *chosen)
{
    int index = env->ui->choose_account(env->ui->ctx, title, list, count);
    if (index < 0 || index >= count) {
        return PICK_CANCELLED;
    }
    *chosen = list[index];
    return PICK_CHOSEN;
}

pick_status_t account_pick(const sync_env_t *env, const char *title, account_t *chosen)
{
    account_t list[ACCOUNT_MAX];
    int count = env->console->list_accounts_with_save(env->console->ctx, list, ACCOUNT_MAX);
    if (count <= 0) {
        return PICK_NO_SAVE;
    }
    if (count == 1) {
        *chosen = list[0];
        return PICK_CHOSEN;
    }
    return choose_from_several(env, title, list, count > ACCOUNT_MAX ? ACCOUNT_MAX : count, chosen);
}

void account_menu_render(const char *title, const account_t *list, int count, int selected, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s\n\n", title);
    for (int i = 0; i < count; i++) {
        size_t used = strlen(out);
        snprintf(out + used, out_size - used, "%s %s\n", i == selected ? ">" : " ", list[i].name);
    }
    size_t used = strlen(out);
    snprintf(out + used, out_size - used, "\n%s\n", TXT_ACCOUNT_MENU_BUTTONS);
}
