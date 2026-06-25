/*
 * Minimal interface to Atmosphère's bpc:ams extension (reboot-to-payload).
 *
 * Vendored from Atmosphère's troposphere/reboot_to_payload (ams_bpc.c/.h).
 * This is NOT part of libnx -- bpc:ams is an Atmosphère-only named port that
 * lets a running app stage an arbitrary fusee/RCM payload into the live
 * ams_mitm reboot buffer, so the next reboot lands in that payload instead
 * of the normal one. We use it to reboot into TegraExplorer, which finalizes
 * a staged CFW update (renaming package3 etc.) before HOS boots.
 *
 * Atmosphère is MIT/BSD-licensed; see Atmosphere-NX/Atmosphere.
 */
#ifndef AMS_BPC_H
#define AMS_BPC_H

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

Result amsBpcInitialize(void);
void   amsBpcExit(void);

/* src_size must be <= 0x24000 (the ams_mitm reboot buffer size). */
Result amsBpcSetRebootPayload(const void *src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif /* AMS_BPC_H */
