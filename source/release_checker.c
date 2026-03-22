#include "release_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
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

/*
 * Resolve a hostname by sending a raw UDP DNS query to 8.8.8.8.
 * This bypasses Atmosphere's DNS MITM (which only hooks sfdnsres,
 * not raw BSD sockets) so we can reach GitHub even when the hosts
 * file is blocking everything.
 */
static bool dnsResolve(const char *hostname, char *out_ip, int out_ip_sz) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return false;

    struct sockaddr_in dns_addr = {0};
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons(53);
    inet_aton("8.8.8.8", &dns_addr.sin_addr);

    /* build DNS query packet */
    unsigned char pkt[512];
    int off = 0;

    /* header: ID=0x1234, flags=0x0100 (standard query, recursion desired) */
    pkt[off++] = 0x12; pkt[off++] = 0x34;  /* ID */
    pkt[off++] = 0x01; pkt[off++] = 0x00;  /* flags */
    pkt[off++] = 0x00; pkt[off++] = 0x01;  /* QDCOUNT = 1 */
    pkt[off++] = 0x00; pkt[off++] = 0x00;  /* ANCOUNT */
    pkt[off++] = 0x00; pkt[off++] = 0x00;  /* NSCOUNT */
    pkt[off++] = 0x00; pkt[off++] = 0x00;  /* ARCOUNT */

    /* encode hostname as DNS labels */
    const char *p = hostname;
    while (*p) {
        const char *dot = strchr(p, '.');
        int label_len = dot ? (int)(dot - p) : (int)strlen(p);
        pkt[off++] = (unsigned char)label_len;
        memcpy(pkt + off, p, label_len);
        off += label_len;
        p += label_len + (dot ? 1 : 0);
        if (!dot) break;
    }
    pkt[off++] = 0x00; /* root label */

    /* QTYPE=A (1), QCLASS=IN (1) */
    pkt[off++] = 0x00; pkt[off++] = 0x01;
    pkt[off++] = 0x00; pkt[off++] = 0x01;

    sendto(sock, pkt, off, 0, (struct sockaddr *)&dns_addr, sizeof(dns_addr));

    /* wait for response with 5 second timeout */
    struct pollfd pfd = { .fd = sock, .events = POLLIN };
    int pr = poll(&pfd, 1, 5000);
    if (pr <= 0) {
        close(sock);
        return false;
    }

    unsigned char resp[512];
    int resp_len = recv(sock, resp, sizeof(resp), 0);
    close(sock);

    if (resp_len < 12) return false;

    /* check we got at least one answer */
    int ancount = (resp[6] << 8) | resp[7];
    if (ancount == 0) return false;

    /* skip the question section */
    int pos = 12;
    while (pos < resp_len && resp[pos] != 0) {
        pos += 1 + resp[pos]; /* skip label */
    }
    pos += 5; /* null byte + QTYPE(2) + QCLASS(2) */

    /* scan answer records for an A record */
    for (int i = 0; i < ancount && pos < resp_len - 10; i++) {
        /* skip name (could be pointer or labels) */
        if ((resp[pos] & 0xC0) == 0xC0)
            pos += 2;
        else {
            while (pos < resp_len && resp[pos] != 0) pos += 1 + resp[pos];
            pos++;
        }

        if (pos + 10 > resp_len) break;

        int rtype  = (resp[pos] << 8) | resp[pos + 1];
        int rdlen  = (resp[pos + 8] << 8) | resp[pos + 9];
        pos += 10;

        if (rtype == 1 && rdlen == 4 && pos + 4 <= resp_len) {
            snprintf(out_ip, out_ip_sz, "%d.%d.%d.%d",
                     resp[pos], resp[pos + 1], resp[pos + 2], resp[pos + 3]);
            return true;
        }

        pos += rdlen;
    }

    return false;
}

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

    /* resolve api.github.com ourselves via raw UDP to 8.8.8.8,
       bypassing Atmosphere's DNS MITM which blocks getaddrinfo */
    char resolved_ip[64] = {0};
    struct curl_slist *resolve_list = NULL;

    if (dnsResolve("api.github.com", resolved_ip, sizeof(resolved_ip))) {
        char resolve_entry[128];
        snprintf(resolve_entry, sizeof(resolve_entry), "api.github.com:443:%s", resolved_ip);
        resolve_list = curl_slist_append(NULL, resolve_entry);
        curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve_list);
    }

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
        curl_slist_free_all(resolve_list);
        free(buf.data);
        return;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(resolve_list);

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
