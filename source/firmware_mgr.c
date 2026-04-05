#include "firmware_mgr.h"
#include "download.h"
#include "extract.h"
#include "config.h"
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

void fwMgrInit(FirmwareManager *fm) {
    memset(fm, 0, sizeof(*fm));
    fm->state = FW_STATE_IDLE;
}

static void *fetch_worker(void *arg) {
    FirmwareManager *fm = arg;

    cJSON *json = NULL;
    if (fetchJson(FIRMWARE_MANIFEST_URL, &json) != 0) {
        fm->state = FW_STATE_ERROR;
        snprintf(fm->error_text, sizeof(fm->error_text), "Failed to fetch firmware list");
        fm->worker_active = false;
        return NULL;
    }

    if (!cJSON_IsArray(json)) {
        cJSON_Delete(json);
        fm->state = FW_STATE_ERROR;
        snprintf(fm->error_text, sizeof(fm->error_text), "Invalid firmware manifest");
        fm->worker_active = false;
        return NULL;
    }

    int size = cJSON_GetArraySize(json);
    fm->count = (size > FW_MAX_ENTRIES) ? FW_MAX_ENTRIES : size;

    for (int i = 0; i < fm->count; i++) {
        cJSON *item = cJSON_GetArrayItem(json, i);
        cJSON *ver = cJSON_GetObjectItem(item, "version");
        cJSON *url = cJSON_GetObjectItem(item, "url");

        if (cJSON_IsString(ver))
            snprintf(fm->entries[i].version, sizeof(fm->entries[i].version),
                     "%s", ver->valuestring);
        if (cJSON_IsString(url))
            snprintf(fm->entries[i].url, sizeof(fm->entries[i].url),
                     "%s", url->valuestring);
    }

    cJSON_Delete(json);
    fm->state = FW_STATE_READY;
    fm->selected = 0;
    snprintf(fm->status_text, sizeof(fm->status_text),
             "%d firmware versions available", fm->count);
    fm->worker_active = false;
    return NULL;
}

static void *download_worker(void *arg) {
    FirmwareManager *fm = arg;
    FirmwareEntry *entry = &fm->entries[fm->selected];

    mkdir(AETHERBLOCK_CONFIG_DIR, 0755);

    if (downloadFile(entry->url, FIRMWARE_DOWNLOAD_PATH, progress_cb, fm) != 0) {
        fm->state = FW_STATE_ERROR;
        snprintf(fm->error_text, sizeof(fm->error_text),
                 "Download failed for firmware %s", entry->version);
        fm->worker_active = false;
        return NULL;
    }

    fm->state = FW_STATE_EXTRACTING;
    fm->progress = 0.0f;
    snprintf(fm->status_text, sizeof(fm->status_text), "Extracting firmware...");

    removeDir(FIRMWARE_EXTRACT_PATH);

    int extract_errs = extractZip(FIRMWARE_DOWNLOAD_PATH, FIRMWARE_EXTRACT_PATH,
                                   NULL, 0, extract_cb, fm);
    if (extract_errs < 0) {
        fm->state = FW_STATE_ERROR;
        snprintf(fm->error_text, sizeof(fm->error_text), "Extraction failed");
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

    char args[256];
    snprintf(args, sizeof(args), "\"%s\" \"%s\"", DAYBREAK_PATH, FIRMWARE_EXTRACT_PATH);
    Result rc = envSetNextLoad(DAYBREAK_PATH, args);
    return R_SUCCEEDED(rc) ? 0 : -1;
}
