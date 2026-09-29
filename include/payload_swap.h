#ifndef PAYLOAD_SWAP_H
#define PAYLOAD_SWAP_H

#include <stdbool.h>

/*
 * Finalizes a staged CFW update that couldn't be applied in-session.
 *
 * Erista: the swap payload is handed to Atmosphère as the reboot payload.
 * Mariko: hekate_ipl.ini is armed to autoboot it once (see payload_swap.c).
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

/* Write sd:/startup.te and load the TegraExplorer payload into memory. On
   Mariko it also writes the payload to bootloader/payloads/ and arms
   hekate_ipl.ini to autoboot it. Must be called while romfs and sdmc are
   still mounted. Returns 0 on success, and fails (rather than guessing) if
   the console type can't be read. After this, call swapArm(). */
int swapPrepare(void);

/* True once swapPrepare() has succeeded and an arm is owed. */
bool swapIsPrepared(void);

/* Clean up after a finished swap: if hekate_ipl.ini still carries our entry,
   restore the user's ini from its backup (or strip our entry); drop a stale
   backup; delete the swap payload only once nothing points at it. Safe to
   call on any console, armed or not. */
void swapDisarmHekate(void);

/* Stage the loaded payload as the next reboot target via Atmosphère's bpc
   extension. MUST be called dead-last, after all other service cleanup,
   because it calls smExit(). If reboot_now is true it also triggers the
   reboot itself (CFW-only path); otherwise it just arms and returns so a
   following Daybreak reboot lands in the payload. On Mariko the arm already
   happened in swapPrepare(), so this just returns 0 and the caller reboots.
   Returns negative and leaves the staged set untouched on failure. */
int swapArm(bool reboot_now);

#endif /* PAYLOAD_SWAP_H */
