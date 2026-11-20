#ifndef TS_PATHS_H
#define TS_PATHS_H

#include <stddef.h>

#define TS_REL_CONFIG "config/totk-sync/config.json"
#define TS_REL_LOG_DIR "switch/totk-sync"
#define TS_REL_LOG "switch/totk-sync/log.txt"
#define TS_REL_BACKUP_DIR "switch/totk-sync/backups"

void paths_fs(const char *sd_root, const char *rel, char *out, size_t out_size);
void paths_display(const char *rel, char *out, size_t out_size);

#endif
