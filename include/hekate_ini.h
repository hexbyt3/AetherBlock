#ifndef HEKATE_INI_H
#define HEKATE_INI_H

/* hekate_ipl.ini parsing and rewriting for the Mariko swap arm. Plain C with
   no libnx dependency, so tests/hekate_ini_test.c can run it on a PC. */

#include <stdbool.h>
#include <stddef.h>

#define HEKATE_LINE_MAX     511   /* hekate reads lines with f_gets(buf, 512) */
#define HEKATE_SWAP_SECTION "AetherBlock Swap"
#define HEKATE_SWAP_PAYLOAD "bootloader/payloads/AetherBlockSwap.bin"

typedef struct {
    char  *buf;
    size_t len, cap;
} StrBuf;

typedef enum { HL_SECTION, HL_CAPTION, HL_COMMENT, HL_NEWLINE, HL_KV } HekLineType;

typedef struct {
    const char *raw;     /* original bytes, for copying through untouched */
    size_t      raw_len;
    bool        has_nl;
    char        text[HEKATE_LINE_MAX + 1];   /* \r and \n removed */
    size_t      text_len;
    HekLineType type;
} HekLine;

typedef struct {
    bool config_first;   /* [config] is the first boot entry in the file */
    bool has_ours;       /* an [AetherBlock Swap] entry exists */
    int  ours_index;     /* hekate's autoboot number for our entry */
    int  entries;        /* boot entries after [config], ours excluded */
    int  autoboot;       /* effective values in [config] */
    int  autoboot_list;
} HekIniInfo;

bool sb_put(StrBuf *sb, const char *s, size_t n);
bool sb_puts(StrBuf *sb, const char *s);

/* Reads the ini the way hekate does. Returns -1 on a line longer than hekate
   reads in one go (we refuse those rather than model hekate's split). */
int  hek_scan(const char *src, size_t len, HekIniInfo *info);

/* True when hekate will boot our entry on its own at the next start. */
bool hek_is_armed(const HekIniInfo *info);

/* Copies the ini with our entry removed and [config]'s autoboot keys replaced
   by `autoboot` (plus autoboot_list=0); `append_ours` adds our entry last. */
int  hek_rewrite(const char *src, size_t len, int autoboot, bool append_ours, StrBuf *out);

#endif
