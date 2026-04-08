#ifndef FIRMWARE_MGR_H
#define FIRMWARE_MGR_H

#include <stdbool.h>
#include <pthread.h>

#define FW_MAX_ENTRIES 64

typedef struct {
    char version[32];
    char url[512];
} FirmwareEntry;

typedef enum {
    FW_STATE_IDLE = 0,
    FW_STATE_FETCHING,
    FW_STATE_READY,
    FW_STATE_DOWNLOADING,
    FW_STATE_EXTRACTING,
    FW_STATE_DONE,
    FW_STATE_ERROR,
} FwMgrState;

typedef struct {
    FirmwareEntry entries[FW_MAX_ENTRIES];
    int           count;
    int           selected;
    FwMgrState    state;
    float         progress;
    char          status_text[256];
    char          error_text[256];
    pthread_t     worker;
    bool          worker_active;
    char          cur_version[16];   /* installed HOS, e.g. "20.1.0" */
} FirmwareManager;

void fwMgrInit(FirmwareManager *fm, const char *current_fw);
void fwMgrStartFetch(FirmwareManager *fm);
void fwMgrStartDownload(FirmwareManager *fm);
int  fwMgrLaunchDaybreak(void);

#endif
