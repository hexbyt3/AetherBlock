#include "self_update.h"
#include "download.h"
#include "pending.h"
#include "config.h"
#include "applog.h"
#include <minizip/unzip.h>
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The website's "latest" link redirects to the newest release's page. Reading
   where it lands costs nothing against the API's hourly limit, which a room
   full of consoles behind one address would share. */
#define LATEST_URL    "https://github.com/hexbyt3/AetherBlock/releases/latest"
#define DOWNLOAD_FMT  "https://github.com/hexbyt3/AetherBlock/releases/download/v%s/AetherBlock.zip"
#define UPDATE_ZIP    AETHERBLOCK_CONFIG_DIR "aetherblock_update.zip"
#define NRO_IN_ZIP    "switch/AetherBlock/AetherBlock.nro"
#define STAGED_NRO    SELF_NRO PENDING_SUFFIX

static bool parse_version(const char *s, unsigned v[3]) {
    if (*s == 'v') s++;
    return sscanf(s, "%u.%u.%u", &v[0], &v[1], &v[2]) == 3;
}

static bool is_newer(const char *candidate, const char *current) {
    unsigned a[3], b[3];
    if (!parse_version(candidate, a) || !parse_version(current, b)) return false;
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

static void set_error(SelfUpdate *su, const char *why) {
    snprintf(su->message, sizeof(su->message), "%s", why);
    su->state = SU_ERROR;
    appLog("self-update: %s", why);
}

static bool find_latest(SelfUpdate *su) {
    char landed[512];
    if (downloadResolveUrl(LATEST_URL, landed, sizeof(landed)) != 0) {
        set_error(su, "Couldn't reach GitHub to check for an AetherBlock update");
        return false;
    }

    const char *tag = strstr(landed, "/releases/tag/");
    unsigned v[3];
    if (!tag || !parse_version(tag + 14, v)) {
        set_error(su, "Couldn't read the newest AetherBlock version");
        return false;
    }
    snprintf(su->latest, sizeof(su->latest), "%u.%u.%u", v[0], v[1], v[2]);
    return true;
}

static void progress_cb(size_t current, size_t total, void *userdata) {
    SelfUpdate *su = userdata;
    if (total > 0) su->progress = (float)current / (float)total;
}

/* pull the one .nro out of the release zip into the staged name; minizip
   checks the entry's CRC when it is closed */
static bool unpack_nro(char *why, size_t why_size) {
    unzFile zf = unzOpen(UPDATE_ZIP);
    if (!zf) {
        snprintf(why, why_size, "The AetherBlock download is not a valid zip");
        return false;
    }

    bool ok = false;
    FILE *out = NULL;
    char *buf = malloc(256 * 1024);
    unsigned char head[0x14] = {0};
    size_t total = 0;

    if (!buf || unzLocateFile(zf, NRO_IN_ZIP, 1) != UNZ_OK ||
        unzOpenCurrentFile(zf) != UNZ_OK) {
        snprintf(why, why_size, "The AetherBlock download has no AetherBlock.nro in it");
        goto done;
    }

    remove(STAGED_NRO);
    out = fopen(STAGED_NRO, "wb");
    if (!out) {
        unzCloseCurrentFile(zf);
        snprintf(why, why_size, "Couldn't write the update to the SD card");
        goto done;
    }

    int n;
    bool wrote = true;
    while ((n = unzReadCurrentFile(zf, buf, 256 * 1024)) > 0) {
        if (total < sizeof(head)) {
            size_t take = sizeof(head) - total;
            if (take > (size_t)n) take = (size_t)n;
            memcpy(head + total, buf, take);
        }
        if (fwrite(buf, 1, (size_t)n, out) != (size_t)n) { wrote = false; break; }
        total += (size_t)n;
    }
    bool closed = fclose(out) == 0;
    out = NULL;
    bool crc_ok = unzCloseCurrentFile(zf) == UNZ_OK;

    if (n < 0 || !crc_ok)
        snprintf(why, why_size, "The AetherBlock download is damaged");
    else if (!wrote || !closed)
        snprintf(why, why_size, "Couldn't write the update to the SD card (is it full?)");
    else if (total < sizeof(head) || memcmp(head + 0x10, "NRO0", 4) != 0)
        snprintf(why, why_size, "The AetherBlock download is not a Switch app");
    else
        ok = true;

done:
    if (out) fclose(out);
    free(buf);
    unzClose(zf);
    if (!ok) remove(STAGED_NRO);
    return ok;
}

static void install(SelfUpdate *su) {
    su->state = SU_DOWNLOADING;
    su->progress = 0.0f;
    snprintf(su->message, sizeof(su->message), "Downloading AetherBlock %s...", su->latest);

    appLogSection("AETHERBLOCK UPDATE");
    appLog("self-update: %s -> %s", APP_VERSION, su->latest);

    char url[256], reason[160];
    snprintf(url, sizeof(url), DOWNLOAD_FMT, su->latest);
    mkdir(AETHERBLOCK_CONFIG_DIR, 0755);

    if (downloadFileChecked(url, UPDATE_ZIP, 0, DOWNLOAD_MAX_ATTEMPTS,
                            progress_cb, su, reason, sizeof(reason)) != 0) {
        char why[160];
        snprintf(why, sizeof(why), "AetherBlock download failed: %.120s", reason);
        set_error(su, why);
        return;
    }

    char why[160];
    bool ok = unpack_nro(why, sizeof(why));
    remove(UPDATE_ZIP);
    if (!ok) {
        set_error(su, why);
        return;
    }

    /* the running .nro can't be replaced while it's open; the next launch
       swaps the staged copy in before it opens anything */
    pendingAdd(SELF_NRO);
    fsdevCommitDevice("sdmc");

    su->progress = 1.0f;
    su->state = SU_READY;
    snprintf(su->message, sizeof(su->message),
             "AetherBlock %s is ready. Press Y to restart into it.", su->latest);
    appLog("self-update: %s staged", su->latest);
}

static void *worker_main(void *arg) {
    SelfUpdate *su = arg;

    if (su->state == SU_CHECKING) {
        if (find_latest(su)) {
            if (is_newer(su->latest, APP_VERSION)) {
                su->state = SU_AVAILABLE;
                snprintf(su->message, sizeof(su->message),
                         "AetherBlock %s is available. Press Y to install it.", su->latest);
            } else {
                su->state = SU_CURRENT;
                snprintf(su->message, sizeof(su->message),
                         "AetherBlock %s is the newest version.", APP_VERSION);
            }
        }
        if (su->state == SU_AVAILABLE && su->install_after_check)
            install(su);
        /* the check at startup wasn't asked for, so it fails silently */
        if (su->state == SU_ERROR && !su->install_after_check) {
            su->state = SU_IDLE;
            su->message[0] = '\0';
        }
    } else {
        install(su);
    }

    su->worker_active = false;
    return NULL;
}

static void start(SelfUpdate *su, SelfUpdateState state, const char *message) {
    if (su->worker_active) return;
    su->state = state;
    snprintf(su->message, sizeof(su->message), "%s", message);
    su->worker_active = true;
    pthread_create(&su->worker, NULL, worker_main, su);
    pthread_detach(su->worker);
}

void selfUpdateCheck(SelfUpdate *su, bool install_too) {
    if (su->worker_active) return;
    su->install_after_check = install_too;
    start(su, SU_CHECKING, "Checking for a newer AetherBlock...");
}

void selfUpdateInstall(SelfUpdate *su) {
    if (su->worker_active || su->latest[0] == '\0') return;
    start(su, SU_DOWNLOADING, "Downloading AetherBlock...");
}

bool selfUpdateRestart(void) {
    return R_SUCCEEDED(envSetNextLoad(SELF_NRO, "\"" SELF_NRO "\""));
}
