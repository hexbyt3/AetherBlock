#ifndef EXTRACT_H
#define EXTRACT_H

typedef void (*ExtractProgressCb)(int current, int total, const char *filename, void *userdata);

int extractZip(const char *zip_path, const char *dest_path,
               const char **preserve_prefixes, int preserve_count,
               ExtractProgressCb cb, void *userdata);

int removeDir(const char *path);

int ensureDirForFile(const char *filepath);

#endif
