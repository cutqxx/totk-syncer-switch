#include "main_screen.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "text.h"

static void append(char *out, size_t out_size, const char *format, ...) __attribute__((format(printf, 3, 4)));

static void append(char *out, size_t out_size, const char *format, ...)
{
    size_t used = strlen(out);
    if (used + 1 >= out_size) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(out + used, out_size - used, format, args);
    va_end(args);
}

static void append_status(char *out, size_t out_size, const pc_status_t *status)
{
    if (status == NULL) {
        append(out, out_size, "%s\n", TXT_STATUS_CHECKING);
        return;
    }
    append(out, out_size, "%s\n", status->status_line);
    if (status->detail[0] != '\0') {
        append(out, out_size, "%s\n", status->detail);
    }
}

static void append_connected_screen(const config_t *cfg, const pc_status_t *status, const char *result, char *out,
                                    size_t out_size)
{
    append(out, out_size, TXT_LABEL_CONFIG "\n", cfg->source_display_path);
    append(out, out_size, TXT_LABEL_PC_ADDRESS "\n\n", cfg->pc_host, cfg->pc_port);
    append_status(out, out_size, status);
    if (result != NULL) {
        append(out, out_size, "\n%s\n", result);
    }
    append(out, out_size, "\n%s\n", TXT_BUTTONS_READY);
}

static void render(const config_t *cfg, const char *blocking_error, const pc_status_t *status, const char *result, char *out,
                   size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    append(out, out_size, "%s\n\n", TXT_TITLE);
    if (cfg == NULL) {
        append(out, out_size, "%s\n\n%s\n", blocking_error, TXT_BUTTONS_BLOCKED);
        return;
    }
    append_connected_screen(cfg, status, result, out, out_size);
}

void main_screen_render(const config_t *cfg, const char *blocking_error, const pc_status_t *status, char *out, size_t out_size)
{
    render(cfg, blocking_error, status, NULL, out, out_size);
}

void main_screen_render_with_result(const config_t *cfg, const pc_status_t *status, const char *result, char *out, size_t out_size)
{
    render(cfg, NULL, status, result, out, out_size);
}
