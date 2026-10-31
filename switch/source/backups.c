#include "backups.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "util.h"

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int backups_prune(const char *dir, int keep) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!str_ends_with(de->d_name, BACKUP_SUFFIX)) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            char **nn = realloc(names, cap * sizeof *nn);
            if (!nn) break;
            names = nn;
        }
        if (!(names[n] = strdup(de->d_name))) break;
        n++;
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp_str);
    int deleted = 0;
    if (keep < 1) keep = 1;
    for (size_t i = 0; i + (size_t)keep < n; i++) {
        char path[768];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if (remove(path) == 0) {
            deleted++;
            log_msg("old backup removed: %s", names[i]);
        }
    }
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);
    return deleted;
}
