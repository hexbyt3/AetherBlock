#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stddef.h>
#include <cJSON.h>

typedef void (*DownloadProgressCb)(size_t current, size_t total, void *userdata);

void downloadGlobalInit(void);
void downloadGlobalCleanup(void);

int downloadFile(const char *url, const char *output_path,
                 DownloadProgressCb cb, void *userdata);

int downloadToMemory(const char *url, char **out_buf, size_t *out_len);

int fetchJson(const char *url, cJSON **out_json);

int checkFreeSpace(long long required_bytes, long long *out_free);

#endif
