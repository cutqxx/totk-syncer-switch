#ifndef TS_FS_COMPAT_H
#define TS_FS_COMPAT_H

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
static inline int ts_mkdir(const char *path) { return _mkdir(path); }
static inline int ts_rmdir(const char *path) { return _rmdir(path); }
#else
#include <unistd.h>
static inline int ts_mkdir(const char *path) { return mkdir(path, 0777); }
static inline int ts_rmdir(const char *path) { return rmdir(path); }
#endif

#endif
