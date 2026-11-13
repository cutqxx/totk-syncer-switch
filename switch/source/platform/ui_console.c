#include "ui_console.h"

#include <stdio.h>
#include <switch.h>

#include "account_pick.h"

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

static void draw_account_menu(const account_t *list, int count, int selected)
{
    char text[512];
    account_menu_render(list, count, selected, text, sizeof text);
    ui_draw_screen(text);
}

static int move_selection(int selected, int count, u64 pressed)
{
    if (pressed & (HidNpadButton_Down | HidNpadButton_AnyDown)) {
        return (selected + 1) % count;
    }
    if (pressed & (HidNpadButton_Up | HidNpadButton_AnyUp)) {
        return (selected + count - 1) % count;
    }
    return selected;
}

bool ui_confirm(void *ctx, const char *prompt)
{
    PadState *pad = ctx;
    ui_show_line(NULL, prompt);
    while (appletMainLoop()) {
        padUpdate(pad);
        u64 pressed = padGetButtonsDown(pad);
        if (pressed & HidNpadButton_A) {
            return true;
        }
        if (pressed & HidNpadButton_B) {
            return false;
        }
        consoleUpdate(NULL);
    }
    return false;
}

int ui_choose_account(void *ctx, const account_t *list, int count)
{
    PadState *pad = ctx;
    int selected = 0;
    draw_account_menu(list, count, selected);
    while (appletMainLoop()) {
        padUpdate(pad);
        u64 pressed = padGetButtonsDown(pad);
        if (pressed & HidNpadButton_A) {
            return selected;
        }
        if (pressed & HidNpadButton_B) {
            return -1;
        }
        int moved_to = move_selection(selected, count, pressed);
        if (moved_to != selected) {
            selected = moved_to;
            draw_account_menu(list, count, selected);
        }
        consoleUpdate(NULL);
    }
    return -1;
}
