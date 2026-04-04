#ifndef CFW_MGR_H
#define CFW_MGR_H

#include <stdbool.h>
#include <pthread.h>

typedef enum {
    CFW_STATE_IDLE = 0,
    CFW_STATE_FETCHING,
    CFW_STATE_READY,
    CFW_STATE_DOWNLOADING,
    CFW_STATE_EXTRACTING,
    CFW_STATE_DONE,
    CFW_STATE_ERROR,
} CfwMgrState;

typedef struct {
    char        current_ams[32];
    char        latest_tag[32];
    char        download_url[512];
    char        changelog[4096];
    bool        update_available;
    bool        is_mariko;
    CfwMgrState state;
    float       progress;
    char        status_text[256];
    char        error_text[256];
    pthread_t   worker;
    bool        worker_active;
} CfwPackageManager;

void cfwMgrInit(CfwPackageManager *cm);
void cfwMgrStartFetch(CfwPackageManager *cm);
void cfwMgrStartDownload(CfwPackageManager *cm);
int  cfwMgrReboot(bool is_mariko);

#endif
