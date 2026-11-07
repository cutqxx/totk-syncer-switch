#ifndef TS_UI_CONSOLE_H
#define TS_UI_CONSOLE_H

void ui_init(void);
void ui_exit(void);

/* Clears the console and prints text (built by main_screen_render, so it never holds the token). */
void ui_draw_screen(const char *text);
void ui_show_line(void *ctx, const char *line);

#endif
