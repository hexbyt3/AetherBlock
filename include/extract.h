#ifndef EXTRACT_H
#define EXTRACT_H

#include <stddef.h>

typedef void (*ExtractProgressCb)(int current, int total, const char *filename, void *userdata);

int extractZip(const char *zip_path, const char *dest_path,
               const char **preserve_prefixes, int preserve_count,
               ExtractProgressCb cb, void *userdata,
               char *failed_out, size_t failed_out_size);

int removeDir(const char *path);

int ensureDirForFile(const char *filepath);

#endif
