#include "payload_swap.h"
#include "ams_bpc.h"
#include "config.h"
#include "applog.h"
#include "pending.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define IRAM_PAYLOAD_MAX_SIZE 0x24000

/* All four version-matched boot files are force-staged (see extract.c) so the
   live set never partially updates: they only ever flip together here, in the
   pre-HOS payload. package3/stratosphere are locked anyway; reboot_payload.bin
   and root fusee.bin are staged on purpose so neither the reboot payload nor
   an RCM-injected fusee can get ahead of package3 (that mismatch is the
   "incorrect fusee version" brick). A failed update leaves the old matched set
   in place -- it can boot, it just isn't updated yet. */
static const char *BOOT_SIDECARS[] = {
    "/atmosphere/package3" PENDING_SUFFIX,
    "/atmosphere/stratosphere.romfs" PENDING_SUFFIX,
    "/atmosphere/reboot_payload.bin" PENDING_SUFFIX,
    "/fusee.bin" PENDING_SUFFIX,
};
#define BOOT_SIDECAR_COUNT (sizeof(BOOT_SIDECARS) / sizeof(BOOT_SIDECARS[0]))

/* Auto-run by stock TegraExplorer (>= v3.0.2) from sd:/startup.te, before any
   menu is drawn. Each block only acts if its sidecar exists, so this is a
   no-op pass-through when nothing is staged. movefile() won't overwrite, so
   the destination is deleted first. payload() only returns on failure, so the
   two calls form a fallback chain; power(3) guarantees a headless unit never
   stalls at the menu if both payloads are somehow missing. */
static const char STARTUP_TE[] =
    "#REQUIRE SD\n"
    "if (fsexists(\"sd:/atmosphere/package3.ab_new\")) {\n"
    "    delfile(\"sd:/atmosphere/package3\")\n"
    "    movefile(\"sd:/atmosphere/package3.ab_new\", \"sd:/atmosphere/package3\")\n"
    "}\n"
    "if (fsexists(\"sd:/atmosphere/stratosphere.romfs.ab_new\")) {\n"
    "    delfile(\"sd:/atmosphere/stratosphere.romfs\")\n"
    "    movefile(\"sd:/atmosphere/stratosphere.romfs.ab_new\", \"sd:/atmosphere/stratosphere.romfs\")\n"
    "}\n"
    "if (fsexists(\"sd:/atmosphere/reboot_payload.bin.ab_new\")) {\n"
    "    delfile(\"sd:/atmosphere/reboot_payload.bin\")\n"
    "    movefile(\"sd:/atmosphere/reboot_payload.bin.ab_new\", \"sd:/atmosphere/reboot_payload.bin\")\n"
    "}\n"
    "if (fsexists(\"sd:/fusee.bin.ab_new\")) {\n"
    "    delfile(\"sd:/fusee.bin\")\n"
    "    movefile(\"sd:/fusee.bin.ab_new\", \"sd:/fusee.bin\")\n"
    "}\n"
    "payload(\"sd:/atmosphere/reboot_payload.bin\")\n"
    "payload(\"sd:/fusee.bin\")\n"
    "power(3)\n";

static u8   g_payload[IRAM_PAYLOAD_MAX_SIZE] __attribute__((aligned(0x1000)));
static bool g_prepared;

bool swapPending(void) {
    struct stat st;
    for (size_t i = 0; i < BOOT_SIDECAR_COUNT; i++)
        if (stat(BOOT_SIDECARS[i], &st) == 0)
            return true;
    return false;
}

bool swapIsPrepared(void) {
    return g_prepared;
}

static int write_startup_script(void) {
    FILE *fp = fopen(STARTUP_TE_PATH, "w");
    if (!fp) {
        appLog("[swap] could not write %s", STARTUP_TE_PATH);
        return -1;
    }
    /* -1 to drop the terminating NUL from the byte count */
    size_t len = sizeof(STARTUP_TE) - 1;
    bool ok = fwrite(STARTUP_TE, 1, len, fp) == len;
    fclose(fp);
    fsdevCommitDevice("sdmc");
    return ok ? 0 : -1;
}

int swapPrepare(void) {
    if (g_prepared)
        return 0;

    if (write_startup_script() != 0)
        return -1;

    FILE *pf = fopen(SWAP_PAYLOAD_ROMFS, "rb");
    if (!pf) {
        appLog("[swap] swap payload missing from romfs (%s)", SWAP_PAYLOAD_ROMFS);
        return -1;
    }
    memset(g_payload, 0, sizeof(g_payload));
    size_t n = fread(g_payload, 1, sizeof(g_payload), pf);
    fclose(pf);
    if (n == 0) {
        appLog("[swap] swap payload read 0 bytes");
        return -1;
    }

    appLog("[swap] startup.te written, %zu-byte payload loaded; swap armed for next reboot", n);
    g_prepared = true;
    return 0;
}

int swapArm(bool reboot_now) {
    if (!g_prepared)
        return -1;

    /* reboot-to-payload is Erista-only; the bpc:ams call early-returns on
       Mariko. Detect first and bail cleanly, leaving the staged set in place
       (the live CFW is still the old consistent one, so nothing breaks). */
    bool erista = true;
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysProductModel model = SetSysProductModel_Invalid;
        if (R_SUCCEEDED(setsysGetProductModel(&model)))
            erista = (model == SetSysProductModel_Nx ||
                      model == SetSysProductModel_Copper);
        setsysExit();
    }
    if (!erista) {
        appLog("[swap] Mariko console -- reboot-to-payload unavailable, "
               "CFW left staged (finish via PC)");
        return -2;
    }

    /* spsm has to be opened while sm is still up if we're going to reboot. */
    if (reboot_now)
        spsmInitialize();

    /* bpc:ams is a named port; sm must be closed before we can reach it. From
       here on we touch only the named port (and, if rebooting, spsm) -- no
       other service calls, no logging. */
    smExit();

    Result rc = amsBpcInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = amsBpcSetRebootPayload(g_payload, IRAM_PAYLOAD_MAX_SIZE);
        amsBpcExit();
    }

    if (reboot_now) {
        /* Whether or not arming succeeded, reboot: on success we land in the
           swap payload; on failure a normal reboot replays the old in-memory
           payload against the still-old package3 -- consistent, no brick. */
        spsmShutdown(true);
    }

    return R_SUCCEEDED(rc) ? 0 : -3;
}
