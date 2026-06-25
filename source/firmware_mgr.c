#include "firmware_mgr.h"
#include "download.h"
#include "extract.h"
#include "pending.h"
#include "payload_swap.h"
#include "config.h"
#include "applog.h"
#include <cJSON.h>
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void progress_cb(size_t current, size_t total, void *userdata) {
    FirmwareManager *fm = userdata;
    if (total > 0)
        fm->progress = (float)current / (float)total;
}

static void extract_cb(int current, int total, const char *filename, void *userdata) {
    FirmwareManager *fm = userdata;
    (void)filename;
    if (total > 0)
        fm->progress = (float)current / (float)total;
}

void fwMgrInit(FirmwareManager *fm, const char *current_fw) {
    memset(fm, 0, sizeof(*fm));
    fm->state = FW_STATE_IDLE;
    if (current_fw)
        snprintf(fm->cur_version, sizeof(fm->cur_version), "%s", current_fw);
}

/* pack a HOS version into one int so we can compare with a single < / > */
static int packVersion(const char *s) {
    unsigned maj = 0, min = 0, mic = 0;
    if (!s || sscanf(s, "%u.%u.%u", &maj, &min, &mic) < 2)
        return -1;
    return (int)((maj << 16) | (min << 8) | mic);
}

static void *fetch_worker(void *arg) {
    FirmwareManager *fm = arg;

    cJSON *json = NULL;
    if (fetchJson(FIRMWARE_RELEASES_URL, &json) != 0) {
        fm->state = FW_STATE_ERROR;
        snprintf(fm->error_text, sizeof(fm->error_text), "Failed to fetch firmware list");
        fm->worker_active = false;
        return NULL;
    }

    if (!cJSON_IsArray(json)) {
        cJSON_Delete(json);
        fm->state = FW_STATE_ERROR;
        snprintf(fm->error_text, sizeof(fm->error_text), "Invalid releases response");
        fm->worker_active = false;
        return NULL;
    }

    int cur_packed = packVersion(fm->cur_version);
    fm->count = 0;

    int release_count = cJSON_GetArraySize(json);
    for (int i = 0; i < release_count && fm->count < FW_MAX_ENTRIES; i++) {
        cJSON *release = cJSON_GetArrayItem(json, i);

        if (cJSON_IsTrue(cJSON_GetObjectItem(release, "draft")) ||
            cJSON_IsTrue(cJSON_GetObjectItem(release, "prerelease")))
            continue;

        cJSON *tag = cJSON_GetObjectItem(release, "tag_name");
        if (!cJSON_IsString(tag)) continue;

        int rel_packed = packVersion(tag->valuestring);
        if (rel_packed < 0) continue;

        /* upgrade-only — never show anything at or below what's installed */
        if (cur_packed >= 0 && rel_packed <= cur_packed)
            continue;

        cJSON *assets = cJSON_GetObjectItem(release, "assets");
        if (!cJSON_IsArray(assets)) continue;

        const char *zip_url = NULL;
        long long zip_size = 0;
        int n = cJSON_GetArraySize(assets);
        for (int j = 0; j < n; j++) {
            cJSON *asset = cJSON_GetArrayItem(assets, j);
            cJSON *name = cJSON_GetObjectItem(asset, "name");
            cJSON *url  = cJSON_GetObjectItem(asset, "browser_download_url");
            if (!cJSON_IsString(name) || !cJSON_IsString(url)) continue;
            if (strstr(name->valuestring, ".zip")) {
                zip_url = url->valuestring;
                cJSON *size = cJSON_GetObjectItem(asset, "size");
                zip_size = cJSON_IsNumber(size) ? (long long)size->valuedouble : 0;
                break;
            }
        }
        if (!zip_url) continue;

        FirmwareEntry *e = &fm->entries[fm->count++];
        snprintf(e->version, sizeof(e->version), "%s", tag->valuestring);
        snprintf(e->url, sizeof(e->url), "%s", zip_url);
        e->size = zip_size;
    }

    cJSON_Delete(json);

    fm->state = FW_STATE_READY;
    fm->selected = 0;
    if (fm->count == 0)
        snprintf(fm->status_text, sizeof(fm->status_text),
                 "Already on latest (%s) — nothing to install", fm->cur_version);
    else
        snprintf(fm->status_text, sizeof(fm->status_text),
                 "%d upgrade%s available from %s",
                 fm->count, fm->count == 1 ? "" : "s", fm->cur_version);
    fm->worker_active = false;
    return NULL;
}

static void *download_worker(void *arg) {
    FirmwareManager *fm = arg;
    FirmwareEntry *entry = &fm->entries[fm->selected];

    mkdir(AETHERBLOCK_CONFIG_DIR, 0755);

    appLogSection("FIRMWARE UPDATE");
    appLog("firmware: %s  url: %s", entry->version, entry->url);
    appLog("expected size: %lld bytes", entry->size);

    char reason[160];
    if (downloadFileChecked(entry->url, FIRMWARE_DOWNLOAD_PATH,
                            entry->size, DOWNLOAD_MAX_ATTEMPTS,
                            progress_cb, fm, reason, sizeof(reason)) != 0) {
        fm->state = FW_STATE_ERROR;
        snprintf(fm->error_text, sizeof(fm->error_text),
                 "Download failed for firmware %s: %s", entry->version, reason);
        appLog("ERROR: firmware download failed after %d attempts: %s",
               DOWNLOAD_MAX_ATTEMPTS, reason);
        fm->worker_active = false;
        return NULL;
    }

    fm->state = FW_STATE_EXTRACTING;
    fm->progress = 0.0f;
    snprintf(fm->status_text, sizeof(fm->status_text), "Extracting firmware...");

    removeDir(FIRMWARE_EXTRACT_PATH);

    int extract_errs = extractZip(FIRMWARE_DOWNLOAD_PATH, FIRMWARE_EXTRACT_PATH,
                                   NULL, 0, extract_cb, fm, NULL, 0, 0);
    if (extract_errs < 0) {
        const char *why;
        switch (extract_errs) {
            case EXTRACT_ERR_OPEN:
                why = "package corrupt - delete firmware.zip and retry"; break;
            case EXTRACT_ERR_INDEX:
                why = "archive index unreadable (corrupt download)"; break;
            case EXTRACT_ERR_MEMORY:
                why = "out of memory"; break;
            default:
                why = "unknown error"; break;
        }
        fm->state = FW_STATE_ERROR;
        snprintf(fm->error_text, sizeof(fm->error_text), "Extraction failed: %s", why);
        appLog("ERROR: firmware extraction aborted (code %d): %s", extract_errs, why);
        fm->worker_active = false;
        return NULL;
    }

    remove(FIRMWARE_DOWNLOAD_PATH);

    fm->state = FW_STATE_DONE;
    fm->progress = 1.0f;
    if (extract_errs > 0)
        snprintf(fm->status_text, sizeof(fm->status_text),
                 "Firmware extracted (%d file%s skipped). Ready for Daybreak.",
                 extract_errs, extract_errs == 1 ? "" : "s");
    else
        snprintf(fm->status_text, sizeof(fm->status_text),
                 "Firmware extracted! Ready for Daybreak.");
    fm->worker_active = false;
    return NULL;
}

void fwMgrStartFetch(FirmwareManager *fm) {
    if (fm->worker_active) return;

    fm->state = FW_STATE_FETCHING;
    fm->count = 0;
    snprintf(fm->status_text, sizeof(fm->status_text), "Fetching firmware list...");
    fm->worker_active = true;
    pthread_create(&fm->worker, NULL, fetch_worker, fm);
    pthread_detach(fm->worker);
}

void fwMgrStartDownload(FirmwareManager *fm) {
    if (fm->worker_active) return;
    if (fm->selected < 0 || fm->selected >= fm->count) return;

    fm->state = FW_STATE_DOWNLOADING;
    fm->progress = 0.0f;
    snprintf(fm->status_text, sizeof(fm->status_text),
             "Downloading firmware %s...", fm->entries[fm->selected].version);
    fm->worker_active = true;
    pthread_create(&fm->worker, NULL, download_worker, fm);
    pthread_detach(fm->worker);
}

int fwMgrLaunchDaybreak(void) {
    struct stat st;
    if (stat(DAYBREAK_PATH, &st) != 0)
        return -1;

    appLogSection("DAYBREAK HANDOFF");

    /* package3 / stratosphere.romfs are locked while Atmosphère runs and can't
       be swapped in-session -- proven by the logs. If any boot file is staged,
       prepare the pre-HOS swap: write sd:/startup.te and load TegraExplorer.
       The actual arm (smExit + bpc) happens dead-last in main(), so Daybreak's
       post-install reboot lands in TegraExplorer, which renames the .ab_new
       files into place and chainloads the now-consistent CFW.

       We deliberately do NOT pendingApply() here: the locked files can't swap
       anyway, and force-staged reboot_payload.bin must stay old until the
       payload flips the whole set at once (that's what prevents a brick). */
    if (swapPending()) {
        appLog("staged CFW boot files present -- arming pre-HOS swap payload");
        if (swapPrepare() != 0)
            appLog("WARNING: could not arm swap payload; CFW will stay on the "
                   "old version (no brick) -- finish via PC if needed");
    } else {
        /* nothing locked this time -- apply any ordinary staged swaps */
        pendingApply();
        appLog("no staged boot files; normal handoff");
    }

    /* flush everything (startup.te, sidecars, pending list) before we lose
       control -- fsdev never commits on its own. */
    Result crc = fsdevCommitDevice("sdmc");
    if (R_FAILED(crc))
        appLog("WARNING: sdmc commit before Daybreak failed (rc=0x%X)", crc);

    char args[256];
    snprintf(args, sizeof(args), "\"%s\" \"%s\"", DAYBREAK_PATH, FIRMWARE_EXTRACT_PATH);
    Result rc = envSetNextLoad(DAYBREAK_PATH, args);
    if (R_FAILED(rc)) return -1;

    /* drop a marker so the next launch of AetherBlock knows to wipe
       /firmware/. we only do this on a successful handoff to Daybreak. */
    mkdir(AETHERBLOCK_CONFIG_DIR, 0755);
    FILE *marker = fopen(FW_CLEANUP_MARKER_PATH, "w");
    if (marker) fclose(marker);

    return 0;
}

void fwMgrCleanupIfPending(void) {
    struct stat st;
    if (stat(FW_CLEANUP_MARKER_PATH, &st) != 0) return;

    removeDir(FIRMWARE_EXTRACT_PATH);
    remove(FW_CLEANUP_MARKER_PATH);
}
