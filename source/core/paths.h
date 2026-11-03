#ifndef TS_PATHS_H
#define TS_PATHS_H

#include <stddef.h>

#define TS_REL_CONFIG "config/totk-sync/config.json"
#define TS_REL_JKSV_CONFIG "config/JKSV/webdav.json"
#define TS_GAME_FOLDER "The Legend of Zelda Tears of the Kingdom"
#define TS_REL_TEST_ZIP "switch/totk-sync/test.zip"
#define TS_REL_CA_PEM "switch/totk-sync/ca.pem"
#define TS_REL_LOG_DIR "switch/totk-sync"
#define TS_REL_LOG "switch/totk-sync/log.txt"

void paths_fs(const char *sd_root, const char *rel, char *out, size_t out_size);
void paths_display(const char *rel, char *out, size_t out_size);

#endif
