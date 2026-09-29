#include "hekate_ini.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool sb_put(StrBuf *sb, const char *s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t ncap = sb->cap ? sb->cap * 2 : 4096;
        while (ncap < sb->len + n + 1)
            ncap *= 2;
        char *nb = realloc(sb->buf, ncap);
        if (!nb)
            return false;
        sb->buf = nb;
        sb->cap = ncap;
    }
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
    return true;
}

bool sb_puts(StrBuf *sb, const char *s) {
    return sb_put(sb, s, strlen(s));
}

/* One line of hekate_ipl.ini as hekate's ini_parse sees it. f_gets drops
   every \r, and lblen counts the \n, so the thresholds below are hekate's
   exactly: '[' opens a boot entry only if lblen > 2, '{' a caption if > 1,
   '#' a comment if > 2, lblen < 2 is a spacer; anything else is a verbatim
   key=value line for the entry above it (last duplicate wins). Returns false
   on a line hekate would split (longer than its 511-byte read); we refuse
   those rather than model the split. */
static bool hek_next_line(const char **pp, const char *end, HekLine *ln) {
    const char *p = *pp;
    const char *nl = memchr(p, '\n', end - p);
    ln->raw = p;
    ln->raw_len = nl ? (size_t)(nl - p) + 1 : (size_t)(end - p);
    ln->has_nl = nl != NULL;
    ln->text_len = 0;
    for (size_t i = 0; i < ln->raw_len; i++) {
        char c = p[i];
        if (c == '\r' || c == '\n')
            continue;
        if (ln->text_len + (ln->has_nl ? 1 : 0) >= HEKATE_LINE_MAX)
            return false;
        ln->text[ln->text_len++] = c;
    }
    ln->text[ln->text_len] = '\0';
    *pp = p + ln->raw_len;

    size_t lblen = ln->text_len + (ln->has_nl ? 1 : 0);
    char c0 = ln->text[0];
    if (lblen > 2 && c0 == '[')
        ln->type = HL_SECTION;
    else if (lblen > 1 && c0 == '{')
        ln->type = HL_CAPTION;
    else if (lblen > 2 && c0 == '#')
        ln->type = HL_COMMENT;
    else if (lblen < 2)
        ln->type = HL_NEWLINE;
    else
        ln->type = HL_KV;
    return true;
}

/* hekate names a section by the text between '[' and the first ']' */
static bool hek_section_is(const HekLine *ln, const char *name) {
    if (ln->type != HL_SECTION)
        return false;
    const char *close = strchr(ln->text + 1, ']');
    size_t len = close ? (size_t)(close - ln->text - 1) : ln->text_len - 1;
    return len == strlen(name) && strncmp(ln->text + 1, name, len) == 0;
}

static bool hek_key_is(const HekLine *ln, const char *key) {
    size_t kl = strlen(key);
    return ln->type == HL_KV && strncmp(ln->text, key, kl) == 0 && ln->text[kl] == '=';
}

int hek_scan(const char *src, size_t len, HekIniInfo *info) {
    memset(info, 0, sizeof(*info));
    const char *p = src, *end = src + len;
    bool first = true, in_config = false;
    int index = 0;
    HekLine ln;

    while (p < end) {
        if (!hek_next_line(&p, end, &ln))
            return -1;
        if (ln.type == HL_SECTION) {
            in_config = false;
            if (first) {
                first = false;
                info->config_first = hek_section_is(&ln, "config");
                in_config = info->config_first;
                continue;
            }
            index++;
            if (hek_section_is(&ln, HEKATE_SWAP_SECTION)) {
                info->has_ours = true;
                info->ours_index = index;
            } else {
                info->entries++;
            }
        } else if (ln.type != HL_KV) {
            in_config = false;
        } else if (in_config) {
            if (hek_key_is(&ln, "autoboot"))
                info->autoboot = atoi(ln.text + 9);
            else if (hek_key_is(&ln, "autoboot_list"))
                info->autoboot_list = atoi(ln.text + 14);
        }
    }
    return 0;
}

/* Armed means hekate will boot our entry on its own at the next start. */
bool hek_is_armed(const HekIniInfo *info) {
    return info->config_first && info->has_ours && info->autoboot_list == 0 &&
           info->autoboot == info->ours_index;
}

/* Copies the ini through with our entry removed and [config]'s autoboot keys
   replaced by `autoboot` (and autoboot_list=0). With `append_ours`, our entry
   is added last -- entries + 1 is then its autoboot number, since hekate
   numbers boot entries 1.. in file order after a leading [config]. */
int hek_rewrite(const char *src, size_t len, int autoboot, bool append_ours, StrBuf *out) {
    const char *eol = strstr(src, "\r\n") ? "\r\n" : "\n";
    const char *p = src, *end = src + len;
    bool in_config = false, in_ours = false;
    char keys[64];
    HekLine ln;

    snprintf(keys, sizeof(keys), "autoboot=%d%sautoboot_list=0%s", autoboot, eol, eol);

    while (p < end) {
        if (!hek_next_line(&p, end, &ln))
            return -1;
        if (ln.type != HL_KV) {
            in_config = hek_section_is(&ln, "config");
            in_ours = hek_section_is(&ln, HEKATE_SWAP_SECTION);
        }
        if (in_ours)
            continue;
        if (in_config && (hek_key_is(&ln, "autoboot") || hek_key_is(&ln, "autoboot_list")))
            continue;
        if (!sb_put(out, ln.raw, ln.raw_len) || (!ln.has_nl && !sb_puts(out, eol)))
            return -1;
        if (in_config && ln.type == HL_SECTION && !sb_puts(out, keys))
            return -1;
    }

    if (!append_ours)
        return 0;
    bool ok = sb_puts(out, eol) &&
              sb_puts(out, "[" HEKATE_SWAP_SECTION "]") && sb_puts(out, eol) &&
              sb_puts(out, "payload=" HEKATE_SWAP_PAYLOAD) && sb_puts(out, eol);
    return ok ? 0 : -1;
}
