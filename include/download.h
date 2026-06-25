#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stddef.h>
#include <cJSON.h>

typedef void (*DownloadProgressCb)(size_t current, size_t total, void *userdata);

void downloadGlobalInit(void);
void downloadGlobalCleanup(void);

int downloadFile(const char *url, const char *output_path,
                 DownloadProgressCb cb, void *userdata);

/* Like downloadFile but retries up to max_attempts and, when
   expected_size > 0, requires the saved file to match that size exactly
   (catching truncated/corrupt transfers that otherwise pass as success).
   On failure a human-readable reason is copied into reason_out.
   Returns 0 on success, -1 once all attempts are exhausted. */
int downloadFileChecked(const char *url, const char *output_path,
                        long long expected_size, int max_attempts,
                        DownloadProgressCb cb, void *userdata,
                        char *reason_out, size_t reason_out_size);

int downloadToMemory(const char *url, char **out_buf, size_t *out_len);

int fetchJson(const char *url, cJSON **out_json);

int checkFreeSpace(long long required_bytes, long long *out_free);

#endif
