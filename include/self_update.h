#ifndef SELF_UPDATE_H
#define SELF_UPDATE_H

#include <stdbool.h>
#include <pthread.h>

/* Updates AetherBlock itself from its own GitHub releases, without
   reinstalling the CFW package: the new .nro is staged next to the running
   one and swapped in by the restart. */

#define SELF_NRO "/switch/AetherBlock/AetherBlock.nro"

typedef enum {
    SU_IDLE = 0,
    SU_CHECKING,
    SU_CURRENT,       /* nothing newer than this build */
    SU_AVAILABLE,
    SU_DOWNLOADING,
    SU_READY,         /* staged; restart to run it */
    SU_ERROR,
} SelfUpdateState;

typedef struct {
    SelfUpdateState state;
    char      latest[24];      /* newest release, without the leading "v" */
    char      message[160];
    float     progress;
    bool      install_after_check;
    bool      announced;       /* the main screen has shown its "available" notice */
    bool      worker_active;
    pthread_t worker;
} SelfUpdate;

/* Look up the newest release in the background. With install set, a newer
   one is downloaded and staged straight away. */
void selfUpdateCheck(SelfUpdate *su, bool install);
/* Download and stage the release a check already found. */
void selfUpdateInstall(SelfUpdate *su);
/* Queue AetherBlock as the next thing to launch; the caller then exits. */
bool selfUpdateRestart(void);

#endif
