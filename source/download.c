#include "download.h"
#include "config.h"
#include "applog.h"
#include <curl/curl.h>
#include <cJSON.h>
#include <switch.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#define USER_AGENT "AetherBlock/" APP_VERSION

typedef struct {
    FILE *fp;
    DownloadProgressCb cb;
    void *userdata;
    long long base;   /* bytes already on disk from earlier attempts */
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

static volatile bool s_abort_all;

void downloadAbortAll(void) {
    s_abort_all = true;
}

/* curl polls this during a transfer; nonzero cancels it */
static int abort_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t ultotal, curl_off_t ulnow) {
    (void)clientp; (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return s_abort_all ? 1 : 0;
}

static int progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    FileWriteCtx *fw = clientp;
    if (fw->cb && dltotal > 0)
        fw->cb((size_t)(fw->base + dlnow), (size_t)(fw->base + dltotal), fw->userdata);
    return s_abort_all ? 1 : 0;
}

CURL *downloadMakeCurl(const char *url) {
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
    /* no overall time limit, but a transfer that crawls for 30 s is dead */
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 4096L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abort_cb);

    return curl;
}

void downloadGlobalInit(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void downloadGlobalCleanup(void) {
    curl_global_cleanup();
}

#define DL_FAILED   (-1)   /* nothing worth keeping; the file is removed */
#define DL_DROPPED  (-2)   /* connection died mid-transfer; the partial file stays */

/* attempts allowed in total when each one keeps getting further */
#define RESUME_ATTEMPT_CAP 60

/* One download attempt, appending from resume_from when it is above zero.
   On failure writes a human-readable cause into reason. Returns 0 on
   success, DL_DROPPED or DL_FAILED otherwise. */
static int download_once(const char *url, const char *output_path,
                         long long resume_from,
                         DownloadProgressCb cb, void *userdata,
                         char *reason, size_t reason_size) {
    FILE *fp = fopen(output_path, resume_from > 0 ? "ab" : "wb");
    if (!fp) {
        if (reason) snprintf(reason, reason_size, "cannot open %s for writing", output_path);
        remove(output_path);
        return DL_FAILED;
    }

    FileWriteCtx ctx = { .fp = fp, .cb = cb, .userdata = userdata, .base = resume_from };

    CURL *curl = downloadMakeCurl(url);
    if (!curl) {
        fclose(fp);
        remove(output_path);
        if (reason) snprintf(reason, reason_size, "curl init failed");
        return DL_FAILED;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    /* keeps an error page out of the file */
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if (resume_from > 0)
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)resume_from);

    if (cb) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    int close_rc = fclose(fp);

    if (res == CURLE_OK && close_rc == 0)
        return 0;

    if (res == CURLE_OK) {
        if (reason) snprintf(reason, reason_size, "could not finish writing %s", output_path);
    } else if (res == CURLE_HTTP_RETURNED_ERROR) {
        if (reason) snprintf(reason, reason_size, "server returned HTTP %ld", http_code);
    } else {
        if (reason) snprintf(reason, reason_size, "network error: %s", curl_easy_strerror(res));
    }

    /* only a connection that died is worth picking up again; a refused
       range, an HTTP error, a full card or a cancel starts clean */
    bool dropped = close_rc == 0 &&
                   (res == CURLE_OPERATION_TIMEDOUT || res == CURLE_PARTIAL_FILE ||
                    res == CURLE_RECV_ERROR || res == CURLE_SEND_ERROR ||
                    res == CURLE_COULDNT_CONNECT || res == CURLE_COULDNT_RESOLVE_HOST ||
                    res == CURLE_SSL_CONNECT_ERROR || res == CURLE_GOT_NOTHING);
    if (dropped && !s_abort_all)
        return DL_DROPPED;

    remove(output_path);
    return DL_FAILED;
}

int downloadFile(const char *url, const char *output_path,
                 DownloadProgressCb cb, void *userdata) {
    char reason[160];
    int rc = download_once(url, output_path, 0, cb, userdata, reason, sizeof(reason));
    if (rc != 0) remove(output_path);
    return rc == 0 ? 0 : -1;
}

static long long file_size(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long long)st.st_size : -1;
}

int downloadFileChecked(const char *url, const char *output_path,
                        long long expected_size, int max_attempts,
                        DownloadProgressCb cb, void *userdata,
                        char *reason_out, size_t reason_out_size) {
    char reason[160] = "no attempt made";
    if (max_attempts < 1) max_attempts = 1;

    /* A dropped transfer picks up where it stopped, and an attempt that got
       further than the last one does not count against max_attempts. Without
       a known size there is nothing to check a stitched file against, so
       those downloads always start over, as does one whose server will not
       resume. */
    long long have = 0;
    int stuck = 0;
    bool can_resume = expected_size > 0;

    for (int attempt = 1; stuck < max_attempts && attempt <= RESUME_ATTEMPT_CAP; attempt++) {
        int rc = download_once(url, output_path, have, cb, userdata, reason, sizeof(reason));
        if (rc != 0) {
            /* a resume the server turned down is not worth asking for twice */
            if (rc == DL_FAILED && have > 0)
                can_resume = false;

            long long got = -1;
            if (rc == DL_DROPPED && can_resume)
                got = file_size(output_path);

            if (got > have && got < expected_size) {
                appLog("download attempt %d dropped at %lld of %lld bytes (%s), resuming",
                       attempt, got, expected_size, reason);
                have = got;
                stuck = 0;
                continue;
            }

            stuck++;
            appLog("download attempt %d failed (%d/%d without progress): %s",
                   attempt, stuck, max_attempts, reason);
            if (got != have || have <= 0) {
                remove(output_path);
                have = 0;
            }
            continue;
        }

        if (expected_size > 0) {
            long long got = file_size(output_path);
            if (got < 0) {
                snprintf(reason, sizeof(reason), "downloaded file vanished before verification");
                appLog("download attempt %d: %s", attempt, reason);
                have = 0;
                stuck++;
                continue;
            }
            if (got != expected_size) {
                snprintf(reason, sizeof(reason), "incomplete: got %lld of %lld bytes",
                         got, expected_size);
                appLog("download attempt %d %s", attempt, reason);
                remove(output_path);
                have = 0;
                stuck++;
                continue;
            }
        }

        appLog("download ok on attempt %d (%lld bytes)", attempt,
               expected_size > 0 ? expected_size : (long long)0);
        if (reason_out && reason_out_size) reason_out[0] = '\0';
        return 0;
    }

    remove(output_path);
    if (reason_out && reason_out_size)
        snprintf(reason_out, reason_out_size, "%s", reason);
    return -1;
}

static int fetch_to_memory(const char *url, const char *range,
                           char **out_buf, size_t *out_len,
                           char *final_url, size_t final_url_size) {
    MemBuf m = { .buf = malloc(4096), .len = 0, .cap = 4096 };
    if (!m.buf) return -1;

    CURL *curl = downloadMakeCurl(url);
    if (!curl) {
        free(m.buf);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m);
    if (range) curl_easy_setopt(curl, CURLOPT_RANGE, range);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (final_url && final_url_size) {
        char *eff = NULL;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
        snprintf(final_url, final_url_size, "%s", eff ? eff : url);
    }
    curl_easy_cleanup(curl);

    /* a ranged request only counts if the server honored the range */
    bool ok = res == CURLE_OK &&
              (range ? http_code == 206 : (http_code >= 200 && http_code < 400));
    if (!ok) {
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

int downloadToMemory(const char *url, char **out_buf, size_t *out_len) {
    return fetch_to_memory(url, NULL, out_buf, out_len, NULL, 0);
}

int downloadRangeToMemory(const char *url, const char *range,
                          char **out_buf, size_t *out_len,
                          char *final_url, size_t final_url_size) {
    return fetch_to_memory(url, range, out_buf, out_len, final_url, final_url_size);
}

int downloadResolveUrl(const char *url, char *final_url, size_t final_url_size) {
    CURL *curl = downloadMakeCurl(url);
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    char *eff = NULL;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
    bool ok = res == CURLE_OK && http_code >= 200 && http_code < 300 && eff;
    if (ok) snprintf(final_url, final_url_size, "%s", eff);
    curl_easy_cleanup(curl);
    return ok ? 0 : -1;
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
