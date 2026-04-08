#ifndef PENDING_H
#define PENDING_H

#define PENDING_LIST_PATH  "/config/AetherBlock/pending.txt"
#define PENDING_SUFFIX     ".ab_new"

/* Records that <target_path>.ab_new is staged and should be swapped in. */
int pendingAdd(const char *target_path);

/* For each entry in the list, rename <target>.ab_new over <target>.
   Entries that succeed (or whose .ab_new is missing) are dropped.
   Entries that still fail are kept for the next attempt.
   Returns the number of swaps that succeeded. */
int pendingApply(void);

/* True if the pending list file exists and is non-empty. */
int pendingHasEntries(void);

#endif
