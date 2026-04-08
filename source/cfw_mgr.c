#include "cfw_mgr.h"
#include "download.h"
#include "extract.h"
#include "pending.h"
#include "cfw_detect.h"
#include "config.h"
#include <cJSON.h>
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *PRESERVE_PATHS[] = {
    "atmosphere/hosts",
    "atmosphere/config/system_settings.ini",
    "atmosphere/config/override_config.ini",
    "atmosphere/contents",
    "config/AetherBlock",
    "Nintendo",
};
#define PRESERVE_COUNT (sizeof(PRESERVE_PATHS) / sizeof(PRESERVE_PATHS[0]))

static void progress_cb(size_t current, size_t total, void *userdata) {
    CfwPackageManager *cm = userdata;
    if (total > 0)
        cm->progress = (float)current / (float)total;
}

static void extract_cb(int current, int total, const char *filename, void *userdata) {
    CfwPackageManager *cm = userdata;
    (void)filename;
    if (total > 0)
        cm->progress = (float)current / (float)total;
}

/* find the right asset for this console type from a CFW4SysBots release */
static int parse_release(cJSON *json, CfwPackageManager *cm) {
    cJSON *tag = cJSON_GetObjectItem(json, "tag_name");
    if (!cJSON_IsString(tag)) return -1;
    snprintf(cm->latest_tag, sizeof(cm->latest_tag), "%s", tag->valuestring);

    cJSON *body = cJSON_GetObjectItem(json, "body");
    if (cJSON_IsString(body))
        snprintf(cm->changelog, sizeof(cm->changelog), "%s", body->valuestring);

    cJSON *assets = cJSON_GetObjectItem(json, "assets");
    if (!cJSON_IsArray(assets)) return -1;

    const char *variant = cm->is_mariko ? CFW_ASSET_MARIKO : CFW_ASSET_ERISTA;

    int count = cJSON_GetArraySize(assets);
    for (int i = 0; i < count; i++) {
        cJSON *asset = cJSON_GetArrayItem(assets, i);
        cJSON *name = cJSON_GetObjectItem(asset, "name");
        cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");

        if (!cJSON_IsString(name) || !cJSON_IsString(url))
            continue;

        if (strstr(name->valuestring, variant) && strstr(name->valuestring, ".zip")) {
            snprintf(cm->download_url, sizeof(cm->download_url),
                     "%s", url->valuestring);
            return 0;
        }
    }
    return -1;
}

void cfwMgrInit(CfwPackageManager *cm) {
    memset(cm, 0, sizeof(*cm));
    cm->state = CFW_STATE_IDLE;
}

static void *fetch_worker(void *arg) {
    CfwPackageManager *cm = arg;

    CfwInfo cfw;
    detectCfwInfo(&cfw);
    snprintf(cm->current_ams, sizeof(cm->current_ams), "%s", cfw.ams_version);
    cm->is_mariko = cfw.is_mariko;

    cJSON *json = NULL;
    if (fetchJson(CFW_API_URL, &json) != 0) {
        cm->state = CFW_STATE_ERROR;
        snprintf(cm->error_text, sizeof(cm->error_text),
                 "Failed to check CFW4SysBots releases");
        cm->worker_active = false;
        return NULL;
    }

    if (parse_release(json, cm) != 0) {
        cJSON_Delete(json);
        cm->state = CFW_STATE_ERROR;
        snprintf(cm->error_text, sizeof(cm->error_text),
                 "No %s package found in latest release",
                 cm->is_mariko ? "mod-chipped" : "unpatched");
        cm->worker_active = false;
        return NULL;
    }
    cJSON_Delete(json);

    cm->update_available = true;
    cm->state = CFW_STATE_READY;
    snprintf(cm->status_text, sizeof(cm->status_text),
             "Latest package: %s (AMS: %s)", cm->latest_tag, cm->current_ams);
    cm->worker_active = false;
    return NULL;
}

static void *download_worker(void *arg) {
    CfwPackageManager *cm = arg;

    mkdir(AETHERBLOCK_CONFIG_DIR, 0755);

    if (downloadFile(cm->download_url, CFW_DOWNLOAD_PATH, progress_cb, cm) != 0) {
        cm->state = CFW_STATE_ERROR;
        snprintf(cm->error_text, sizeof(cm->error_text), "Download failed");
        cm->worker_active = false;
        return NULL;
    }

    cm->state = CFW_STATE_EXTRACTING;
    cm->progress = 0.0f;
    snprintf(cm->status_text, sizeof(cm->status_text), "Extracting CFW package...");

    cm->failed_files[0] = '\0';
    int extract_errs = extractZip(CFW_DOWNLOAD_PATH, "/",
                                   PRESERVE_PATHS, PRESERVE_COUNT,
                                   extract_cb, cm,
                                   cm->failed_files, sizeof(cm->failed_files),
                                   1);
    if (extract_errs < 0) {
        cm->state = CFW_STATE_ERROR;
        snprintf(cm->error_text, sizeof(cm->error_text), "Extraction failed");
        cm->worker_active = false;
        return NULL;
    }

    remove(CFW_DOWNLOAD_PATH);

    cm->state = CFW_STATE_DONE;
    cm->progress = 1.0f;
    if (extract_errs > 0)
        snprintf(cm->status_text, sizeof(cm->status_text),
                 "CFW %s installed, %d file%s failed. Reboot to apply.",
                 cm->latest_tag, extract_errs, extract_errs == 1 ? "" : "s");
    else
        snprintf(cm->status_text, sizeof(cm->status_text),
                 "CFW package %s installed! Reboot to apply.", cm->latest_tag);
    cm->worker_active = false;
    return NULL;
}

void cfwMgrStartFetch(CfwPackageManager *cm) {
    if (cm->worker_active) return;

    cm->state = CFW_STATE_FETCHING;
    snprintf(cm->status_text, sizeof(cm->status_text), "Checking for updates...");
    cm->worker_active = true;
    pthread_create(&cm->worker, NULL, fetch_worker, cm);
    pthread_detach(cm->worker);
}

void cfwMgrStartDownload(CfwPackageManager *cm) {
    if (cm->worker_active) return;

    cm->state = CFW_STATE_DOWNLOADING;
    cm->progress = 0.0f;
    snprintf(cm->status_text, sizeof(cm->status_text),
             "Downloading CFW package %s...", cm->latest_tag);
    cm->worker_active = true;
    pthread_create(&cm->worker, NULL, download_worker, cm);
    pthread_detach(cm->worker);
}

int cfwMgrReboot(bool is_mariko) {
    /* swap any files that were staged as .ab_new during extraction
       (package3, stratosphere.romfs, AetherBlock.nro, etc.) — do this
       right before the reboot call so nothing else has a chance to
       reopen them first */
    pendingApply();

    Result rc;
    if (is_mariko) {
        rc = spsmInitialize();
        if (R_FAILED(rc)) return -1;
        rc = spsmShutdown(true);
        spsmExit();
    } else {
        rc = bpcInitialize();
        if (R_FAILED(rc)) return -1;
        rc = bpcRebootSystem();
        bpcExit();
    }
    return R_SUCCEEDED(rc) ? 0 : -1;
}
