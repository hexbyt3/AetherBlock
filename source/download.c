#include "download.h"
#include "config.h"
#include "applog.h"
#include <curl/curl.h>
#include <cJSON.h>
#include <switch.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#define USER_AGENT "AetherBlock/" APP_VERSION

typedef struct {
    FILE *fp;
    DownloadProgressCb cb;
    void *userdata;
} FileWriteCtx;

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} MemBuf;

static size_t write_file_cb(void *ptr, size_t size, size_t nmemb, void *ctx) {
    FileWriteCtx *fw = ctx;
    return fwrite(ptr, size, nmemb, fw->fp);
}

static size_t write_mem_cb(void *ptr, size_t size, size_t nmemb, void *ctx) {
    MemBuf *m = ctx;
    size_t bytes = size * nmemb;
    if (m->len + bytes >= m->cap) {
        size_t newcap = (m->cap + bytes) * 2;
        char *tmp = realloc(m->buf, newcap);
        if (!tmp) return 0;
        m->buf = tmp;
        m->cap = newcap;
    }
    memcpy(m->buf + m->len, ptr, bytes);
    m->len += bytes;
    return bytes;
}

static int progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    FileWriteCtx *fw = clientp;
    if (fw->cb && dltotal > 0)
        fw->cb((size_t)dlnow, (size_t)dltotal, fw->userdata);
    return 0;
}

static CURL *make_curl(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 256 * 1024L);

    return curl;
}

void downloadGlobalInit(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void downloadGlobalCleanup(void) {
    curl_global_cleanup();
}

/* one download attempt; on failure writes a human-readable cause into
   reason and removes any partial file. Returns 0 on success. */
static int download_once(const char *url, const char *output_path,
                         DownloadProgressCb cb, void *userdata,
                         char *reason, size_t reason_size) {
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        if (reason) snprintf(reason, reason_size, "cannot open %s for writing", output_path);
        return -1;
    }

    FileWriteCtx ctx = { .fp = fp, .cb = cb, .userdata = userdata };

    CURL *curl = make_curl(url);
    if (!curl) {
        fclose(fp);
        remove(output_path);
        if (reason) snprintf(reason, reason_size, "curl init failed");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    if (cb) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        if (reason) snprintf(reason, reason_size, "network error: %s", curl_easy_strerror(res));
        remove(output_path);
        return -1;
    }
    if (http_code < 200 || http_code >= 400) {
        if (reason) snprintf(reason, reason_size, "server returned HTTP %ld", http_code);
        remove(output_path);
        return -1;
    }
    return 0;
}

int downloadFile(const char *url, const char *output_path,
                 DownloadProgressCb cb, void *userdata) {
    char reason[160];
    return download_once(url, output_path, cb, userdata, reason, sizeof(reason));
}

int downloadFileChecked(const char *url, const char *output_path,
                        long long expected_size, int max_attempts,
                        DownloadProgressCb cb, void *userdata,
                        char *reason_out, size_t reason_out_size) {
    char reason[160] = "no attempt made";
    if (max_attempts < 1) max_attempts = 1;

    for (int attempt = 1; attempt <= max_attempts; attempt++) {
        if (download_once(url, output_path, cb, userdata, reason, sizeof(reason)) != 0) {
            appLog("download attempt %d/%d failed: %s", attempt, max_attempts, reason);
            continue;
        }

        if (expected_size > 0) {
            struct stat st;
            if (stat(output_path, &st) != 0) {
                snprintf(reason, sizeof(reason), "downloaded file vanished before verification");
                appLog("download attempt %d/%d: %s", attempt, max_attempts, reason);
                continue;
            }
            if ((long long)st.st_size != expected_size) {
                snprintf(reason, sizeof(reason), "incomplete: got %lld of %lld bytes",
                         (long long)st.st_size, expected_size);
                appLog("download attempt %d/%d %s", attempt, max_attempts, reason);
                remove(output_path);
                continue;
            }
        }

        appLog("download ok on attempt %d/%d (%lld bytes)", attempt, max_attempts,
               expected_size > 0 ? expected_size : (long long)0);
        if (reason_out && reason_out_size) reason_out[0] = '\0';
        return 0;
    }

    if (reason_out && reason_out_size)
        snprintf(reason_out, reason_out_size, "%s", reason);
    return -1;
}

int downloadToMemory(const char *url, char **out_buf, size_t *out_len) {
    MemBuf m = { .buf = malloc(4096), .len = 0, .cap = 4096 };
    if (!m.buf) return -1;

    CURL *curl = make_curl(url);
    if (!curl) {
        free(m.buf);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code < 200 || http_code >= 400) {
        free(m.buf);
        return -1;
    }

    char *tmp = realloc(m.buf, m.len + 1);
    if (!tmp) {
        free(m.buf);
        return -1;
    }
    m.buf = tmp;
    m.buf[m.len] = '\0';

    *out_buf = m.buf;
    *out_len = m.len;
    return 0;
}

int fetchJson(const char *url, cJSON **out_json) {
    char *buf = NULL;
    size_t len = 0;

    if (downloadToMemory(url, &buf, &len) != 0)
        return -1;

    cJSON *json = cJSON_Parse(buf);
    free(buf);

    if (!json) return -1;

    *out_json = json;
    return 0;
}

int checkFreeSpace(long long required_bytes, long long *out_free) {
    s64 free_space = 0;
    Result rc = nsInitialize();
    if (R_FAILED(rc)) return -1;

    rc = nsGetFreeSpaceSize(NcmStorageId_SdCard, &free_space);
    nsExit();
    if (R_FAILED(rc)) return -1;

    if (out_free) *out_free = (long long)free_space;
    return (free_space >= required_bytes) ? 0 : 1;
}
