#ifndef WORKER_H
#define WORKER_H

#include <stdbool.h>

/* Background tasks. Every thread started here is joined before the app
   exits. That matters more than it looks: libnx can't clean up a detached
   thread, so its stack stays mapped -- and the heap pages behind it stay
   locked -- for whatever homebrew runs next in the same session. A second
   AetherBlock then allocates into those pages and takes a data abort. */
bool workerStart(void *(*fn)(void *), void *arg);

/* Stop transfers in flight and wait for every task to finish. */
void workerShutdown(void);

/* Call first thing in main(): unlock heap pages that a previous run left
   behind as thread stacks (versions up to 2.1.6 did). Returns the number of
   pages released. */
int workerReleaseLeakedStacks(void);

#endif
