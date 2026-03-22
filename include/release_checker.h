#ifndef RELEASE_CHECKER_H
#define RELEASE_CHECKER_H

#include <switch.h>
#include <stdbool.h>

#define GITHUB_API_URL  "https://api.github.com/repos/Atmosphere-NX/Atmosphere/releases/latest"
#define RELEASE_TAG_MAX     32
#define RELEASE_NAME_MAX    128
#define RELEASE_FW_MAX      32
#define RELEASE_BODY_MAX    4096
#define RELEASE_DATE_MAX    32

typedef enum {
    RELEASE_IDLE = 0,
    RELEASE_FETCHING,
    RELEASE_SUCCESS,
    RELEASE_ERROR,
} ReleaseCheckState;

typedef struct {
    /* local system info */
    int fw_major, fw_minor, fw_micro;
    int ams_major, ams_minor, ams_micro;
    bool ams_detected;

    /* latest release from GitHub */
    char tag_name[RELEASE_TAG_MAX];
    char release_name[RELEASE_NAME_MAX];
    char supported_fw[RELEASE_FW_MAX];
    char body[RELEASE_BODY_MAX];
    char published_date[RELEASE_DATE_MAX];
    int  latest_major, latest_minor, latest_micro;

    /* state */
    ReleaseCheckState state;
    char error_msg[256];
    bool update_available;

    /* background thread */
    Thread fetch_thread;
    bool   thread_running;
} ReleaseInfo;

void releaseInit(ReleaseInfo *ri);
void releaseReadLocal(ReleaseInfo *ri);
void releaseFetchAsync(ReleaseInfo *ri);
bool releaseFetchDone(ReleaseInfo *ri);

#endif
