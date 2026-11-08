#ifndef TS_UI_CONSOLE_H
#define TS_UI_CONSOLE_H

#include "account.h"

void ui_init(void);
void ui_exit(void);

/* Clears the console and prints text (built by main_screen_render, so it never holds the token). */
void ui_draw_screen(const char *text);
void ui_show_line(void *ctx, const char *line);

/* ui_port_t.choose_account; ctx is the PadState* of the main loop. Up/Down move, A picks (returns the index), B cancels (-1).
   The shared PadState matters: an A that is still held from the main screen does not count as a new press. */
int ui_choose_account(void *ctx, const account_t *list, int count);

#endif
