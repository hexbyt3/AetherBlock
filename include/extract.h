#ifndef EXTRACT_H
#define EXTRACT_H

#include <stddef.h>

typedef void (*ExtractProgressCb)(int current, int total, const char *filename, void *userdata);

/* If stage_locked_files is non-zero, any file that can't be written
   in place (locked by a running sysmodule, the app's own NRO, etc.)
   is written to <path>.ab_new instead and appended to the pending list
   for later swap. Such files are NOT counted as errors. */
int extractZip(const char *zip_path, const char *dest_path,
               const char **preserve_prefixes, int preserve_count,
               ExtractProgressCb cb, void *userdata,
               char *failed_out, size_t failed_out_size,
               int stage_locked_files);

int removeDir(const char *path);

int ensureDirForFile(const char *filepath);

#endif
