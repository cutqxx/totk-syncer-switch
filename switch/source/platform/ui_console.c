#include "ui_console.h"

#include <stdio.h>
#include <switch.h>

void ui_init(void)
{
    consoleInit(NULL);
}

void ui_exit(void)
{
    consoleExit(NULL);
}

void ui_draw_screen(const char *text)
{
    consoleClear();
    fputs(text, stdout);
    consoleUpdate(NULL);
}

void ui_show_line(void *ctx, const char *line)
{
    (void)ctx;
    printf("%s\n", line);
    consoleUpdate(NULL);
}
