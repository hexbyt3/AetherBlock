/* Host test for source/hekate_ini.c. Build and run:
 *   cc -std=c11 -Wall -Wextra -Iinclude tests/hekate_ini_test.c source/hekate_ini.c -o ini_test && ./ini_test
 *
 * hekate_model_* below is an independent transcription of hekate v6.5.4's
 * ini_parse (bdk/utils/ini.c, with FatFs f_gets under FF_USE_STRFUNC 2) and
 * the entry selection in _auto_launch (bootloader/main.c). Every armed ini we
 * produce is fed through it to prove hekate itself would boot our entry. */

#include "hekate_ini.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { M_CHOICE, M_CAPTION, M_COMMENT, M_NEWLINE };

typedef struct {
    int  type;
    char name[512];
    char payload[512];
    int  autoboot, autoboot_list;
    bool has_autoboot, has_autoboot_list;
} MSection;

typedef struct {
    MSection sec[256];
    int      count;
} MIni;

/* f_gets(buf, 512): up to 511 chars, \r dropped, stops after \n */
static const char *model_fgets(const char *p, const char *end, char *buf) {
    int n = 0;
    while (p < end && n < 511) {
        char c = *p++;
        if (c == '\r')
            continue;
        buf[n++] = c;
        if (c == '\n')
            break;
    }
    buf[n] = '\0';
    return p;
}

static void model_parse(const char *src, MIni *ini) {
    const char *p = src, *end = src + strlen(src);
    char lbuf[512];
    MSection *csec = NULL;
    memset(ini, 0, sizeof(*ini));

    do {
        p = model_fgets(p, end, lbuf);
        size_t lblen = strlen(lbuf);
        if (lblen && lbuf[lblen - 1] == '\n')
            lbuf[lblen - 1] = 0;

        if (lblen > 2 && lbuf[0] == '[') {
            csec = &ini->sec[ini->count++];
            char *close = strchr(lbuf, ']');
            if (close) *close = 0;
            csec->type = M_CHOICE;
            snprintf(csec->name, sizeof(csec->name), "%s", lbuf + 1);
        } else if (lblen > 1 && lbuf[0] == '{') {
            csec = &ini->sec[ini->count++];
            csec->type = M_CAPTION;
        } else if (lblen > 2 && lbuf[0] == '#') {
            csec = &ini->sec[ini->count++];
            csec->type = M_COMMENT;
        } else if (lblen < 2) {
            csec = &ini->sec[ini->count++];
            csec->type = M_NEWLINE;
        } else if (csec && csec->type == M_CHOICE) {
            char *eq = strchr(lbuf, '=');
            char *val = eq ? eq + 1 : lbuf + strlen(lbuf);
            if (eq) *eq = 0;
            if (!strcmp(lbuf, "autoboot"))      { csec->autoboot = atoi(val); csec->has_autoboot = true; }
            if (!strcmp(lbuf, "autoboot_list")) { csec->autoboot_list = atoi(val); csec->has_autoboot_list = true; }
            if (!strcmp(lbuf, "payload"))       snprintf(csec->payload, sizeof(csec->payload), "%s", val);
        }
    } while (p < end);
}

/* _auto_launch without boot_cfg overrides: returns the booted section or NULL
   (menu). List mode never reaches hekate_ipl.ini entries, so it's NULL too. */
static const MSection *model_autoboot(const MIni *ini) {
    int autoboot = 0, autoboot_list = 0, id = 0;
    bool config_found = false;
    for (int i = 0; i < ini->count; i++) {
        const MSection *s = &ini->sec[i];
        if (s->type != M_CHOICE)
            continue;
        if (!config_found && !strcmp(s->name, "config")) {
            config_found = true;
            if (s->has_autoboot) autoboot = s->autoboot;
            if (s->has_autoboot_list) autoboot_list = s->autoboot_list;
            id++;
            if (autoboot_list)
                return NULL;
            continue;
        }
        if (autoboot == id && config_found)
            return s;
        id++;
    }
    return NULL;
}

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool boots_ours(const char *ini_text) {
    static MIni m;
    model_parse(ini_text, &m);
    const MSection *s = model_autoboot(&m);
    return s && !strcmp(s->name, HEKATE_SWAP_SECTION) && !strcmp(s->payload, HEKATE_SWAP_PAYLOAD);
}

static const char *model_boot_name(const char *ini_text) {
    static MIni m;
    model_parse(ini_text, &m);
    const MSection *s = model_autoboot(&m);
    return s ? s->name : "(menu)";
}

/* arm like arm_hekate does; returns malloc'd armed ini or NULL if refused */
static char *arm(const char *src) {
    HekIniInfo info;
    if (hek_scan(src, strlen(src), &info) != 0 || !info.config_first)
        return NULL;
    StrBuf out = {0};
    if (hek_rewrite(src, strlen(src), info.entries + 1, true, &out) != 0) {
        free(out.buf);
        return NULL;
    }
    return out.buf;
}

static void expect_armed(const char *label, const char *src) {
    char *armed = arm(src);
    CHECK(armed != NULL, "%s: refused to arm", label);
    if (!armed)
        return;
    CHECK(boots_ours(armed), "%s: hekate would boot %s", label, model_boot_name(armed));

    HekIniInfo info;
    CHECK(hek_scan(armed, strlen(armed), &info) == 0 && hek_is_armed(&info),
          "%s: hek_is_armed disagrees with hekate", label);

    /* arming twice must be stable */
    char *again = arm(armed);
    CHECK(again && boots_ours(again), "%s: re-arm broke it", label);

    /* stripping our entry must leave hekate at the menu, never on our entry */
    StrBuf stripped = {0};
    CHECK(hek_rewrite(armed, strlen(armed), 0, false, &stripped) == 0 && stripped.buf &&
          !strcmp(model_boot_name(stripped.buf), "(menu)"), "%s: strip did not disarm", label);

    free(stripped.buf);
    free(again);
    free(armed);
}

static void expect_refused(const char *label, const char *src) {
    char *armed = arm(src);
    CHECK(armed == NULL, "%s: armed an ini it should refuse", label);
    free(armed);
}

static void expect_not_armed(const char *label, const char *src) {
    HekIniInfo info;
    bool armed = hek_scan(src, strlen(src), &info) == 0 && hek_is_armed(&info);
    CHECK(!armed, "%s: reported armed", label);
    CHECK(!boots_ours(src), "%s: model boots ours (test input wrong)", label);
}

static char *long_line_ini(size_t pad, const char *tail) {
    char *s = malloc(pad + 256);
    strcpy(s, "[config]\nautoboot=0\n#");
    size_t n = strlen(s);
    memset(s + n, 'x', pad);
    strcpy(s + n + pad, tail);
    return s;
}

int main(void) {
    const char *pkg =
        "[config]\nautoboot=0\nautoboot_list=0\nbootwait=3\nverification=1\nbackight=100\n"
        "autohosoff=0\nautonogc=1\nupdater2p=1\n\n{DeepSea}\n{Github: x}\n\n"
        "{--- Custom Firmware ---}\n[CFW (SYSNAND)]\nemummc_force_disable=1\nfss0=atmosphere/package3\n"
        "atmosphere=1\n{}\n\n[CFW (EMUMMC)]\nemummcforce=1\nfss0=atmosphere/package3\n{}\n\n"
        "{--- Stock ---}\n[Stock (SYSNAND)]\nemummc_force_disable=1\nfss0=atmosphere/package3\nstock=1\n{}\n";

    expect_armed("package ini (LF)", pkg);

    /* same file with CRLF, and without a trailing newline */
    {
        StrBuf crlf = {0};
        for (const char *p = pkg; *p; p++) {
            if (*p == '\n') sb_puts(&crlf, "\r\n");
            else sb_put(&crlf, p, 1);
        }
        expect_armed("package ini (CRLF)", crlf.buf);
        char *armed = arm(crlf.buf);
        CHECK(armed && strstr(armed, "\r\n[" HEKATE_SWAP_SECTION "]\r\n"), "CRLF: appended entry not CRLF");
        free(armed);
        free(crlf.buf);
    }
    expect_armed("no trailing newline", "[config]\nautoboot=0\n[A]\nfss0=x");
    expect_armed("existing autoboot + list mode", "[config]\nautoboot=2\nautoboot_list=1\n[A]\n[B]\n");
    expect_armed("only [config]", "[config]\n");
    expect_armed("only [config], no newline", "[config]");
    expect_armed("caption right after [config]", "[config]\n{cap}\nautoboot=1\n[A]\n");
    expect_armed("stale swap entry mid-file", "[config]\n[A]\n[" HEKATE_SWAP_SECTION "]\npayload=x\n[B]\n");
    expect_armed("duplicate [config]", "[config]\n[A]\n[config]\nautoboot=1\n[B]\n");
    expect_armed("empty [] entry", "[config]\n[]\n[A]\n");
    expect_armed("header with trailing text", "[config] ;x\nautoboot=0\n[A]\n");

    /* reviewer case: a bare '#' or '[' inside [config] is a key line to
       hekate, so autoboot keys after it still count */
    expect_armed("bare # inside [config]", "[config]\n#\nautoboot=1\n[A]\n[B]\n");
    expect_armed("bare [ inside [config]", "[config]\n[\nautoboot_list=1\n[A]\n");
    expect_armed("stray CRs", "[config]\r\n[\r\r\nautoboot=1\r\n[A]\r\n");
    expect_armed("\"autoboot =1\" is not hekate's key", "[config]\nautoboot =1\n[A]\n");

    /* lines hekate would split are refused, not guessed at */
    {
        char *s = long_line_ini(600, "[A]\n[B]\n");
        expect_refused("line over 511 chars", s);
        free(s);
        s = long_line_ini(508, "\n[A]\n");      /* "#"+508+"\n" = 510 chars: fits */
        expect_armed("line of 510 chars", s);
        free(s);
        s = long_line_ini(509, "\n[A]\n");      /* 511 with the \n: f_gets still reads it whole */
        expect_armed("line of 511 chars incl newline", s);
        free(s);
        s = long_line_ini(510, "\n[A]\n");      /* 512: hekate splits it */
        expect_refused("line of 512 chars", s);
        free(s);
    }

    expect_refused("[config] not first", "[A]\n[config]\n[B]\n");
    expect_refused("no [config]", "[A]\n[B]\n");
    expect_refused("BOM before [config]", "\xEF\xBB\xBF[config]\n[A]\n");

    /* hek_is_armed must not be fooled by a leftover entry the ini no longer
       autoboots (the stale-backup case from review) */
    expect_not_armed("our entry, autoboot=0", "[config]\nautoboot=0\n[A]\n[" HEKATE_SWAP_SECTION "]\npayload=" HEKATE_SWAP_PAYLOAD "\n");
    expect_not_armed("our entry, list mode", "[config]\nautoboot=2\nautoboot_list=1\n[A]\n[" HEKATE_SWAP_SECTION "]\npayload=" HEKATE_SWAP_PAYLOAD "\n");
    expect_not_armed("package ini", pkg);

    /* restoring from the armed ini must give the user's own entries back in
       the same order, with only [config]'s autoboot touched */
    {
        const char *user = "[config]\nautoboot=1\nbootwait=0\n[CFW]\nfss0=a\n\n[Stock]\nstock=1\n";
        char *armed = arm(user);
        StrBuf back = {0};
        CHECK(armed && hek_rewrite(armed, strlen(armed), 1, false, &back) == 0 &&
              !strcmp(model_boot_name(back.buf), "CFW") && strstr(back.buf, "[Stock]\nstock=1\n") &&
              strstr(back.buf, "bootwait=0\n"), "round trip lost user entries");
        free(back.buf);
        free(armed);
    }

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all hekate_ini checks passed\n");
    return 0;
}
