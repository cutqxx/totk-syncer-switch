#include "paths.h"

#include <stdio.h>

void paths_fs(const char *sd_root, const char *rel, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s", sd_root, rel);
}

void paths_display(const char *rel, char *out, size_t out_size)
{
    snprintf(out, out_size, "sd:/%s", rel);
}
