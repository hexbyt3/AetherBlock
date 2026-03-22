#include "release_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>
#include <jansson.h>

void releaseInit(ReleaseInfo *ri) {
    memset(ri, 0, sizeof(*ri));
    ri->state = RELEASE_IDLE;
}

void releaseReadLocal(ReleaseInfo *ri) {
    u32 ver = hosversionGet();
    ri->fw_major = (ver >> 16) & 0xFF;
    ri->fw_minor = (ver >> 8) & 0xFF;
    ri->fw_micro = ver & 0xFF;

    ri->ams_detected = false;
    if (hosversionIsAtmosphere()) {
        Result rc = splInitialize();
        if (R_SUCCEEDED(rc)) {
            u64 val = 0;
            rc = splGetConfig((SplConfigItem)65000, &val);
            if (R_SUCCEEDED(rc)) {
                ri->ams_major = (val >> 56) & 0xFF;
                ri->ams_minor = (val >> 48) & 0xFF;
                ri->ams_micro = (val >> 40) & 0xFF;
                ri->ams_detected = true;
            }
            splExit();
        }
    }
}

typedef struct {
    char  *data;
    size_t size;
    size_t capacity;
} CurlBuffer;

static size_t writeCb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    CurlBuffer *buf = (CurlBuffer *)userdata;
    size_t bytes = size * nmemb;

    if (buf->size + bytes + 1 > buf->capacity) {
        size_t new_cap = buf->capacity ? buf->capacity * 2 : 8192;
        while (new_cap < buf->size + bytes + 1) new_cap *= 2;
        char *tmp = realloc(buf->data, new_cap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->size, ptr, bytes);
    buf->size += bytes;
    buf->data[buf->size] = '\0';
    return bytes;
}

/* Scan release body for firmware version patterns like "19.0.0" */
static void extractSupportedFw(ReleaseInfo *ri) {
    ri->supported_fw[0] = '\0';
    const char *p = ri->body;

    while (*p) {
        if (isdigit((unsigned char)*p)) {
            int major = 0, minor = 0, micro = 0;
            int n = 0;
            if (sscanf(p, "%d.%d.%d%n", &major, &minor, &micro, &n) == 3 && n > 0) {
                if (major >= 1 && major <= 30 && minor <= 20 && micro <= 20) {
                    snprintf(ri->supported_fw, RELEASE_FW_MAX, "%d.%d.%d", major, minor, micro);

                    const char *context_start = (p > ri->body + 40) ? p - 40 : ri->body;
                    char context[80];
                    int clen = (int)(p - context_start);
                    if (clen > 79) clen = 79;
                    memcpy(context, context_start, clen);
                    context[clen] = '\0';

                    if (strstr(context, "irmware") || strstr(context, "FW") ||
                        strstr(context, "fw") || strstr(context, "upport")) {
                        return;
                    }
                    p += n;
                    continue;
                }
            }
        }
        p++;
    }
}

static bool versionGreater(int a_maj, int a_min, int a_mic,
                           int b_maj, int b_min, int b_mic) {
    if (a_maj != b_maj) return a_maj > b_maj;
    if (a_min != b_min) return a_min > b_min;
    return a_mic > b_mic;
}

/* the actual fetch logic, called from the background thread */
static void releaseFetchInternal(ReleaseInfo *ri) {
    ri->error_msg[0] = '\0';
    ri->update_available = false;

    CURL *curl = curl_easy_init();
    if (!curl) {
        ri->state = RELEASE_ERROR;
        snprintf(ri->error_msg, sizeof(ri->error_msg), "Failed to initialize curl");
        return;
    }

    CurlBuffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, GITHUB_API_URL);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AetherBlock/1.0.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        ri->state = RELEASE_ERROR;
        snprintf(ri->error_msg, sizeof(ri->error_msg), "Network error: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(buf.data);
        return;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        ri->state = RELEASE_ERROR;
        if (http_code == 403)
            snprintf(ri->error_msg, sizeof(ri->error_msg), "GitHub API rate limited (HTTP 403). Try again later.");
        else
            snprintf(ri->error_msg, sizeof(ri->error_msg), "GitHub API returned HTTP %ld", http_code);
        free(buf.data);
        return;
    }

    json_error_t jerr;
    json_t *root = json_loads(buf.data, 0, &jerr);
    free(buf.data);

    if (!root) {
        ri->state = RELEASE_ERROR;
        snprintf(ri->error_msg, sizeof(ri->error_msg), "Failed to parse JSON response");
        return;
    }

    const char *tag = json_string_value(json_object_get(root, "tag_name"));
    const char *name = json_string_value(json_object_get(root, "name"));
    const char *body = json_string_value(json_object_get(root, "body"));
    const char *date = json_string_value(json_object_get(root, "published_at"));

    if (tag) {
        const char *t = tag;
        if (*t == 'v' || *t == 'V') t++;
        strncpy(ri->tag_name, t, RELEASE_TAG_MAX - 1);
        sscanf(ri->tag_name, "%d.%d.%d", &ri->latest_major, &ri->latest_minor, &ri->latest_micro);
    }
    if (name) strncpy(ri->release_name, name, RELEASE_NAME_MAX - 1);
    if (body) strncpy(ri->body, body, RELEASE_BODY_MAX - 1);
    if (date) {
        strncpy(ri->published_date, date, RELEASE_DATE_MAX - 1);
        char *tee = strchr(ri->published_date, 'T');
        if (tee) *tee = '\0';
    }

    json_decref(root);

    extractSupportedFw(ri);

    if (ri->ams_detected) {
        ri->update_available = versionGreater(
            ri->latest_major, ri->latest_minor, ri->latest_micro,
            ri->ams_major, ri->ams_minor, ri->ams_micro);
    }

    ri->state = RELEASE_SUCCESS;
}

static void fetchThreadFunc(void *arg) {
    ReleaseInfo *ri = (ReleaseInfo *)arg;
    releaseFetchInternal(ri);
    ri->thread_running = false;
}

void releaseFetchAsync(ReleaseInfo *ri) {
    if (ri->thread_running) return;

    ri->state = RELEASE_FETCHING;
    ri->thread_running = true;

    Result rc = threadCreate(&ri->fetch_thread, fetchThreadFunc, ri, NULL, 0x10000, 0x2C, -2);
    if (R_FAILED(rc)) {
        ri->state = RELEASE_ERROR;
        ri->thread_running = false;
        snprintf(ri->error_msg, sizeof(ri->error_msg), "Failed to create thread (rc=0x%X)", rc);
        return;
    }

    rc = threadStart(&ri->fetch_thread);
    if (R_FAILED(rc)) {
        ri->state = RELEASE_ERROR;
        ri->thread_running = false;
        threadClose(&ri->fetch_thread);
        snprintf(ri->error_msg, sizeof(ri->error_msg), "Failed to start thread (rc=0x%X)", rc);
    }
}

bool releaseFetchDone(ReleaseInfo *ri) {
    if (!ri->thread_running && ri->state != RELEASE_FETCHING)
        return true;

    if (!ri->thread_running) {
        threadWaitForExit(&ri->fetch_thread);
        threadClose(&ri->fetch_thread);
        return true;
    }

    return false;
}
