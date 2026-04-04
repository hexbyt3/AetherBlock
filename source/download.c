#include "download.h"
#include "config.h"
#include <curl/curl.h>
#include <cJSON.h>
#include <switch.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

int downloadFile(const char *url, const char *output_path,
                 DownloadProgressCb cb, void *userdata) {
    FILE *fp = fopen(output_path, "wb");
    if (!fp) return -1;

    FileWriteCtx ctx = { .fp = fp, .cb = cb, .userdata = userdata };

    CURL *curl = make_curl(url);
    if (!curl) {
        fclose(fp);
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

    if (res != CURLE_OK || http_code < 200 || http_code >= 400) {
        remove(output_path);
        return -1;
    }
    return 0;
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
