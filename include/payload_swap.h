#ifndef PAYLOAD_SWAP_H
#define PAYLOAD_SWAP_H

#include <stdbool.h>

/*
 * Finalizes a staged CFW update that couldn't be applied in-session.
 *
 * package3 / stratosphere.romfs (and, to keep the boot set atomic,
 * reboot_payload.bin) are locked while Atmosphère runs and can only be
 * replaced before HOS boots. We stage them as <file>.ab_new, then reboot into
 * TegraExplorer, whose sd:/startup.te renames the sidecars into place and
 * chainloads the now-consistent CFW. The old set stays fully in place until
 * that swap runs, so a failure here can never brick -- it just boots the old
 * CFW and leaves the sidecars for a retry.
 */

/* True if any boot-critical .ab_new sidecar is waiting to be swapped in. */
bool swapPending(void);

/* Write sd:/startup.te and load the TegraExplorer payload into memory.
   Must be called while romfs and sdmc are still mounted. Returns 0 on
   success. After this, call swapArm() once services are being torn down. */
int swapPrepare(void);

/* True once swapPrepare() has succeeded and an arm is owed. */
bool swapIsPrepared(void);

/* Stage the loaded payload as the next reboot target via Atmosphère's bpc
   extension. MUST be called dead-last, after all other service cleanup,
   because it calls smExit(). If reboot_now is true it also triggers the
   reboot itself (CFW-only path); otherwise it just arms and returns so a
   following Daybreak reboot lands in the payload. Erista-only; returns
   negative and leaves the staged set untouched on Mariko or on failure. */
int swapArm(bool reboot_now);

#endif /* PAYLOAD_SWAP_H */
