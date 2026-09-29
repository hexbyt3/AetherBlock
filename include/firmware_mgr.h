#ifndef FIRMWARE_MGR_H
#define FIRMWARE_MGR_H

#include <stdbool.h>
#include <pthread.h>

#define FW_MAX_ENTRIES 64

typedef struct {
    char      version[32];
    char      url[512];
    long long size;       /* expected asset size in bytes, 0 if unknown */
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
#define FW_LAUNCH_NO_DAYBREAK  -1
#define FW_LAUNCH_CFW_STAGED   -2   /* staged CFW can't be finalized first */

/* Hand off to Daybreak. Returns 0, or a FW_LAUNCH_* error; refuses when a
   staged CFW update couldn't be guaranteed to swap in on Daybreak's reboot. */
int  fwMgrLaunchDaybreak(void);

/* Undo a handoff (next load back to hbmenu, keep /firmware/). Makes no
   service calls, so it's safe after swapArm() has closed sm. */
void fwMgrCancelDaybreak(void);

/* If a "firmware cleanup pending" marker was left behind by a previous
   launch of Daybreak, wipe the staged /firmware/ directory and clear
   the marker. Safe to call unconditionally on startup. */
void fwMgrCleanupIfPending(void);

#endif
