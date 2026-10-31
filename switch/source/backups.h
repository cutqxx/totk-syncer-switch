// SD-card backups made before every pull: <dir>/<timestamp>_before_pull.zip
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define BACKUP_SUFFIX "_before_pull.zip"

// Deletes the oldest "*_before_pull.zip" files in `dir`, keeping `keep` newest
// (names start with a sortable timestamp). Other files are never touched.
// Returns the number of deleted files, or -1 if the folder can't be read.
int backups_prune(const char *dir, int keep);
