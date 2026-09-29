#include "payload_swap.h"
#include "ams_bpc.h"
#include "config.h"
#include "applog.h"
#include "pending.h"
#include "hekate_ini.h"
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define IRAM_PAYLOAD_MAX_SIZE 0x24000

/* The version-matched boot files are force-staged (see extract.c) so the live
   set never partially updates: they only ever flip together here, in the
   pre-HOS payload. package3/stratosphere are locked anyway; reboot_payload.bin
   and the fusee copies are staged on purpose so neither the reboot payload nor
   an injected fusee can get ahead of package3 (that mismatch is the
   "incorrect fusee version" brick). A failed update leaves the old matched set
   in place -- it can boot, it just isn't updated yet. */
static const char *BOOT_SIDECARS[] = {
    "/atmosphere/package3" PENDING_SUFFIX,
    "/atmosphere/stratosphere.romfs" PENDING_SUFFIX,
    "/atmosphere/reboot_payload.bin" PENDING_SUFFIX,
    "/fusee.bin" PENDING_SUFFIX,
    "/bootloader/payloads/fusee.bin" PENDING_SUFFIX,   /* mod-chipped package */
};
#define BOOT_SIDECAR_COUNT (sizeof(BOOT_SIDECARS) / sizeof(BOOT_SIDECARS[0]))

/* Swaps every staged boot file; shared by both consoles' scripts. Each block
   only acts if its sidecar exists, so re-running it is harmless -- an
   interrupted swap just finishes on the next run. movefile() won't overwrite,
   so the destination is deleted first. */
#define TE_SWAP_BLOCK(path) \
    "if (fsexists(\"sd:" path ".ab_new\")) {\n" \
    "    delfile(\"sd:" path "\")\n" \
    "    movefile(\"sd:" path ".ab_new\", \"sd:" path "\")\n" \
    "}\n"

#define TE_SWAP_ALL \
    TE_SWAP_BLOCK("/atmosphere/package3") \
    TE_SWAP_BLOCK("/atmosphere/stratosphere.romfs") \
    TE_SWAP_BLOCK("/atmosphere/reboot_payload.bin") \
    TE_SWAP_BLOCK("/fusee.bin") \
    TE_SWAP_BLOCK("/bootloader/payloads/fusee.bin")

/* Auto-run by TegraExplorer from sd:/startup.te before any menu is drawn (our
   build, romfs/TegraExplorer.bin, is 4.2.0 plus
   tools/tegraexplorer/autorun-no-wait.patch so it never stops for input).
   payload() only returns on failure, so the payload calls form a fallback
   chain; power(3) powers off rather than leave a headless unit at the menu. */
static const char STARTUP_TE[] =
    "#REQUIRE SD\n"
    TE_SWAP_ALL
    "payload(\"sd:/atmosphere/reboot_payload.bin\")\n"
    "payload(\"sd:/fusee.bin\")\n"
    "power(3)\n";

#define HEKATE_INI          "/bootloader/hekate_ipl.ini"
#define HEKATE_INI_BAK      HEKATE_INI ".ab_hkbak"
#define HEKATE_INI_TMP      HEKATE_INI ".ab_hktmp"
#define HEKATE_INI_OLD      HEKATE_INI ".ab_hkold"
#define HEKATE_INI_MAX      (64 * 1024)

/* Mariko has no reboot-to-payload, but its mod chip boots hekate, so there the
   swap payload is armed through hekate: TegraExplorer is copied into
   bootloader/payloads/ and hekate_ipl.ini is set to autoboot it. The script
   swaps the boot set FIRST and puts the user's ini back LAST, so if power is
   lost mid-swap hekate is still armed and the next boot simply finishes the
   job. Then it chainloads hekate for the user's normal boot. */
static const char STARTUP_TE_MARIKO[] =
    "#REQUIRE SD\n"
    TE_SWAP_ALL
    "if (fsexists(\"sd:" HEKATE_INI_BAK "\")) {\n"
    "    delfile(\"sd:" HEKATE_INI "\")\n"
    "    movefile(\"sd:" HEKATE_INI_BAK "\", \"sd:" HEKATE_INI "\")\n"
    "}\n"
    "payload(\"sd:/bootloader/update.bin\")\n"
    "payload(\"sd:/bootloader/payloads/fusee.bin\")\n"
    "power(3)\n";

static u8     g_payload[IRAM_PAYLOAD_MAX_SIZE] __attribute__((aligned(0x1000)));
static size_t g_payload_len;
static bool   g_prepared;
static bool   g_mariko;

/* Same source as cfw_detect.c (SPL hardware type: 0/1 Erista, 2+ Mariko), but
   an unreadable type is an error here -- guessing wrong picks the wrong swap
   mechanism, and the Erista one silently does nothing on Mariko. */
static int detect_mariko(bool *mariko) {
    u64 hw_type = 0;
    Result rc = splInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = splGetConfig(SplConfigItem_HardwareType, &hw_type);
        splExit();
    }
    if (R_FAILED(rc)) {
        appLog("[swap] could not read the hardware type (rc=0x%X)", rc);
        return -1;
    }
    *mariko = hw_type >= 2;
    return 0;
}

static char *read_small_file(const char *path, size_t max, size_t *len_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    char *buf = malloc(max + 1);
    size_t n = buf ? fread(buf, 1, max + 1, fp) : 0;
    fclose(fp);
    if (!buf || n == 0 || n > max) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    *len_out = n;
    return buf;
}

static int write_file(const char *path, const void *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -1;
    bool ok = fwrite(data, 1, len, fp) == len;
    fclose(fp);
    return ok ? 0 : -1;
}

static bool exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Replaces the live ini with `data` without ever leaving the card without
   one: write a temp copy, move the live file aside, move the temp in. */
static int hek_replace_ini(const char *data, size_t len) {
    remove(HEKATE_INI_TMP);
    remove(HEKATE_INI_OLD);
    if (write_file(HEKATE_INI_TMP, data, len) != 0) {
        remove(HEKATE_INI_TMP);
        return -1;
    }
    if (rename(HEKATE_INI, HEKATE_INI_OLD) != 0) {
        remove(HEKATE_INI_TMP);
        return -1;
    }
    if (rename(HEKATE_INI_TMP, HEKATE_INI) != 0) {
        rename(HEKATE_INI_OLD, HEKATE_INI);
        remove(HEKATE_INI_TMP);
        return -1;
    }
    remove(HEKATE_INI_OLD);
    return 0;
}

/* Point hekate's autoboot at the swap payload. "Armed" is judged from the
   live ini, never from the backup alone: a CFW update rewrites hekate_ipl.ini
   from the package, so an old backup can outlive the arm it belonged to. */
static int arm_hekate(void) {
    size_t len = 0;
    char *ini = read_small_file(HEKATE_INI, HEKATE_INI_MAX, &len);
    if (!ini && exists(HEKATE_INI_BAK) && rename(HEKATE_INI_BAK, HEKATE_INI) == 0)
        ini = read_small_file(HEKATE_INI, HEKATE_INI_MAX, &len);
    if (!ini) {
        appLog("[swap] could not read %s", HEKATE_INI);
        return -1;
    }

    HekIniInfo info;
    if (hek_scan(ini, len, &info) != 0 || !info.config_first) {
        free(ini);
        appLog("[swap] %s: [config] is not the first entry, or a line is longer "
               "than hekate reads -- not arming", HEKATE_INI);
        return -1;
    }

    if (hek_is_armed(&info) && exists(HEKATE_INI_BAK)) {
        free(ini);
        appLog("[swap] hekate already armed");
        return 0;
    }

    /* The user's config to restore after the swap: the live ini if it has no
       trace of us; else the existing backup; else the live ini with our entry
       stripped and autoboot left at the menu. */
    StrBuf user = {0}, armed = {0};
    int rc = -1;
    if (!info.has_ours) {
        rc = sb_put(&user, ini, len) ? 0 : -1;
    } else {
        size_t blen = 0;
        char *bak = read_small_file(HEKATE_INI_BAK, HEKATE_INI_MAX, &blen);
        if (bak)
            rc = sb_put(&user, bak, blen) ? 0 : -1;
        else
            rc = hek_rewrite(ini, len, 0, false, &user);
        free(bak);
    }
    if (rc == 0)
        rc = hek_rewrite(ini, len, info.entries + 1, true, &armed);
    free(ini);

    if (rc == 0) {
        remove(HEKATE_INI_BAK);
        rc = write_file(HEKATE_INI_BAK, user.buf, user.len);
    }
    if (rc == 0)
        rc = hek_replace_ini(armed.buf, armed.len);
    free(user.buf);
    free(armed.buf);

    if (rc != 0) {
        remove(HEKATE_INI_BAK);
        appLog("[swap] could not arm %s", HEKATE_INI);
        return -1;
    }

    fsdevCommitDevice("sdmc");
    appLog("[swap] hekate armed: autoboot=%d -> [" HEKATE_SWAP_SECTION "]", info.entries + 1);
    return 0;
}

void swapDisarmHekate(void) {
    size_t len = 0;
    char *ini = read_small_file(HEKATE_INI, HEKATE_INI_MAX, &len);
    if (!ini) {
        /* no live ini at all: a backup is the best config there is */
        if (exists(HEKATE_INI_BAK) && rename(HEKATE_INI_BAK, HEKATE_INI) == 0)
            appLog("[swap] %s was missing; restored it from the backup", HEKATE_INI);
        fsdevCommitDevice("sdmc");
        return;
    }

    HekIniInfo info;
    bool ours = hek_scan(ini, len, &info) == 0 && info.has_ours;

    if (!ours) {
        /* the live ini is the user's; a backup here is stale */
        free(ini);
        remove(HEKATE_INI_BAK);
        remove("/" HEKATE_SWAP_PAYLOAD);
        fsdevCommitDevice("sdmc");
        return;
    }

    /* our entry is still in the live ini: hand back the backup, or strip our
       entry out ourselves. The payload stays until the ini no longer points
       at it -- hekate stops at "Payload file is missing!" otherwise. */
    bool clean = false;
    if (exists(HEKATE_INI_BAK)) {
        size_t blen = 0;
        char *bak = read_small_file(HEKATE_INI_BAK, HEKATE_INI_MAX, &blen);
        if (bak && hek_replace_ini(bak, blen) == 0) {
            remove(HEKATE_INI_BAK);
            clean = true;
            appLog("[swap] restored %s from its backup", HEKATE_INI);
        }
        free(bak);
    } else {
        StrBuf out = {0};
        if (hek_rewrite(ini, len, 0, false, &out) == 0 && hek_replace_ini(out.buf, out.len) == 0) {
            clean = true;
            appLog("[swap] removed a leftover swap entry from %s (autoboot reset to the menu)",
                   HEKATE_INI);
        }
        free(out.buf);
    }
    free(ini);

    if (clean)
        remove("/" HEKATE_SWAP_PAYLOAD);
    fsdevCommitDevice("sdmc");
}

bool swapPending(void) {
    for (size_t i = 0; i < BOOT_SIDECAR_COUNT; i++)
        if (exists(BOOT_SIDECARS[i]))
            return true;
    return false;
}

bool swapIsPrepared(void) {
    return g_prepared;
}

static int write_startup_script(const char *script, size_t size) {
    FILE *fp = fopen(STARTUP_TE_PATH, "w");
    if (!fp) {
        appLog("[swap] could not write %s", STARTUP_TE_PATH);
        return -1;
    }
    /* -1 to drop the terminating NUL from the byte count */
    size_t len = size - 1;
    bool ok = fwrite(script, 1, len, fp) == len;
    fclose(fp);
    fsdevCommitDevice("sdmc");
    return ok ? 0 : -1;
}

int swapPrepare(void) {
    if (g_prepared)
        return 0;

    if (detect_mariko(&g_mariko) != 0)
        return -1;

    int rc = g_mariko ? write_startup_script(STARTUP_TE_MARIKO, sizeof(STARTUP_TE_MARIKO))
                      : write_startup_script(STARTUP_TE, sizeof(STARTUP_TE));
    if (rc != 0)
        return -1;

    FILE *pf = fopen(SWAP_PAYLOAD_ROMFS, "rb");
    if (!pf) {
        appLog("[swap] swap payload missing from romfs (%s)", SWAP_PAYLOAD_ROMFS);
        return -1;
    }
    memset(g_payload, 0, sizeof(g_payload));
    size_t n = fread(g_payload, 1, sizeof(g_payload), pf);
    bool too_big = fgetc(pf) != EOF;
    fclose(pf);
    if (n == 0 || too_big) {
        appLog("[swap] swap payload unusable (%zu bytes read%s)", n,
               too_big ? ", larger than the 0x24000 limit" : "");
        return -1;
    }
    g_payload_len = n;

    if (g_mariko) {
        /* the payload goes down before the ini is armed, so hekate is never
           pointed at a file that isn't there yet */
        mkdir("/bootloader", 0755);
        mkdir("/bootloader/payloads", 0755);
        if (write_file("/" HEKATE_SWAP_PAYLOAD, g_payload, g_payload_len) != 0) {
            appLog("[swap] could not write /%s", HEKATE_SWAP_PAYLOAD);
            return -1;
        }
        fsdevCommitDevice("sdmc");
        if (arm_hekate() != 0)
            return -1;
        appLog("[swap] Mariko: startup.te written, %zu-byte payload armed through hekate", n);
        g_prepared = true;
        return 0;
    }

    appLog("[swap] startup.te written, %zu-byte payload loaded; swap armed for next reboot", n);
    g_prepared = true;
    return 0;
}

int swapArm(bool reboot_now) {
    if (!g_prepared)
        return -1;

    /* Mariko was armed through hekate in swapPrepare(), so any reboot lands in
       the swap payload; there's nothing to hand bpc:ams (Erista-only anyway).
       The caller does the reboot. */
    if (g_mariko)
        return 0;

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
