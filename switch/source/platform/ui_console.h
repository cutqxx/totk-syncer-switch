#ifndef TS_UI_CONSOLE_H
#define TS_UI_CONSOLE_H

#include "config.h"
#include "experiment.h"

void ui_init(void);
void ui_exit(void);

/* Never receives or prints the password. cfg is NULL when the config failed to load. init_error is shown prominently when not empty. */
void ui_draw_menu(const config_t *cfg, const experiment_t *experiment, const char *config_error, const char *destination_url, const char *init_error);
void ui_show_line(void *ctx, const char *line);

#endif
