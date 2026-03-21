#include "hosts_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

static const char *CATEGORY_NAMES[CATEGORY_COUNT] = {
    "Telemetry",
    "System/Firmware Updates",
    "Game Content",
    "eShop",
    "Other",
};

const char *categoryName(HostCategory cat) {
    if (cat >= 0 && cat < CATEGORY_COUNT)
        return CATEGORY_NAMES[cat];
    return "Unknown";
}

HostCategory hostsInferCategory(const char *hostname) {
    if (!hostname) return CATEGORY_OTHER;

    if (strstr(hostname, "receive-lp1.dg.srv.nintendo.net")) return CATEGORY_TELEMETRY;
    if (strstr(hostname, "receive-lp1.er.srv.nintendo.net")) return CATEGORY_TELEMETRY;
    if (strstr(hostname, "realtime-receive-lp1.dg.srv.nintendo.net")) return CATEGORY_TELEMETRY;
    if (strstr(hostname, "sprofile-lp1.cdn.nintendo.net")) return CATEGORY_TELEMETRY;

    if (strstr(hostname, "sun.hac.lp1.d4c.nintendo.net")) return CATEGORY_SYSTEM_UPDATES;
    if (strstr(hostname, "atumn.hac.lp1.d4c.nintendo.net")) return CATEGORY_SYSTEM_UPDATES;

    if (strstr(hostname, "atum.hac.lp1.d4c.nintendo.net")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "atum-")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "superfly.hac.lp1.d4c.nintendo.net")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "aqua.hac.lp1.d4c.nintendo.net")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "aquarius.hac.lp1.d4c.nintendo.net")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "dragons.hac.lp1")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "tagaya.hac.lp1.eshop.nintendo.net")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "beach.hac.lp1.eshop.nintendo.net")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "pearljam.hac.lp1.eshop.nintendo.net")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "pushmo.hac.lp1.eshop.nintendo.net")) return CATEGORY_GAME_CONTENT;
    if (strstr(hostname, "veer.hac.lp1.d4c.nintendo.net")) return CATEGORY_GAME_CONTENT;

    if (strstr(hostname, "ecs-lp1.hac.shop.nintendo.net")) return CATEGORY_ESHOP;
    if (strstr(hostname, "ias-lp1.hac.shop.nintendo.net")) return CATEGORY_ESHOP;
    if (strstr(hostname, "bugyo.hac.lp1.eshop.nintendo.net")) return CATEGORY_ESHOP;

    return CATEGORY_OTHER;
}

static void trimNewline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

void hostsParseLine(HostEntry *e, const char *line) {
    memset(e, 0, sizeof(*e));
    strncpy(e->raw_line, line, HOSTS_MAX_LINE_LEN - 1);
    trimNewline(e->raw_line);

    const char *p = e->raw_line;

    while (*p && isspace((unsigned char)*p)) p++;

    if (*p == '\0') {
        e->is_blank = true;
        return;
    }

    if (*p == '#') {
        e->is_comment = true;
        return;
    }

    /* disabled entries start with ';' */
    if (*p == ';') {
        e->enabled = false;
        p++;
        while (*p && isspace((unsigned char)*p)) p++;
    } else {
        e->enabled = true;
    }

    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < 63) {
        e->ip[i++] = *p++;
    }
    e->ip[i] = '\0';

    while (*p && isspace((unsigned char)*p)) p++;

    i = 0;
    while (*p && !isspace((unsigned char)*p) && i < 191) {
        e->hostname[i++] = *p++;
    }
    e->hostname[i] = '\0';

    e->category = hostsInferCategory(e->hostname);
}

static bool fileExists(const char *path) {
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static void detectActivePath(HostsFile *hf) {
    if (fileExists(HOSTS_PATH_EMUMMC)) {
        strncpy(hf->active_path, HOSTS_PATH_EMUMMC, sizeof(hf->active_path) - 1);
    } else if (fileExists(HOSTS_PATH_SYSMMC)) {
        strncpy(hf->active_path, HOSTS_PATH_SYSMMC, sizeof(hf->active_path) - 1);
    } else {
        strncpy(hf->active_path, HOSTS_PATH_DEFAULT, sizeof(hf->active_path) - 1);
    }
}

Result hostsLoad(HostsFile *hf) {
    memset(hf, 0, sizeof(*hf));
    detectActivePath(hf);

    FILE *f = fopen(hf->active_path, "r");
    if (!f) return 0;

    char line[HOSTS_MAX_LINE_LEN];
    while (fgets(line, sizeof(line), f) && hf->entry_count < HOSTS_MAX_ENTRIES) {
        hostsParseLine(&hf->entries[hf->entry_count], line);
        hf->entry_count++;
    }

    fclose(f);
    hf->dirty = false;
    return 0;
}

Result hostsSave(HostsFile *hf) {
    char tmp_path[260];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", hf->active_path);

    FILE *f = fopen(tmp_path, "w");
    if (!f) return MAKERESULT(Module_Libnx, 1);

    for (int i = 0; i < hf->entry_count; i++) {
        HostEntry *e = &hf->entries[i];

        if (e->is_blank) {
            fprintf(f, "\n");
        } else if (e->is_comment) {
            fprintf(f, "%s\n", e->raw_line);
        } else if (e->enabled) {
            fprintf(f, "%s %s\n", e->ip, e->hostname);
        } else {
            fprintf(f, ";%s %s\n", e->ip, e->hostname);
        }
    }

    fflush(f);
    fclose(f);

    remove(hf->active_path);
    if (rename(tmp_path, hf->active_path) != 0) {
        return MAKERESULT(Module_Libnx, 2);
    }

    hf->dirty = false;
    return 0;
}

void hostsToggleEntry(HostsFile *hf, int index) {
    if (index < 0 || index >= hf->entry_count) return;

    HostEntry *e = &hf->entries[index];
    if (e->is_comment || e->is_blank) return;

    e->enabled = !e->enabled;
    hf->dirty = true;
}

bool hostsAddEntry(HostsFile *hf, const char *ip, const char *hostname, bool enabled, HostCategory cat) {
    if (hf->entry_count >= HOSTS_MAX_ENTRIES) return false;

    for (int i = 0; i < hf->entry_count; i++) {
        if (!hf->entries[i].is_comment && !hf->entries[i].is_blank) {
            if (strcmp(hf->entries[i].hostname, hostname) == 0)
                return false;
        }
    }

    HostEntry *e = &hf->entries[hf->entry_count];
    memset(e, 0, sizeof(*e));
    strncpy(e->ip, ip, sizeof(e->ip) - 1);
    strncpy(e->hostname, hostname, sizeof(e->hostname) - 1);
    e->enabled = enabled;
    e->category = cat;

    if (enabled)
        snprintf(e->raw_line, sizeof(e->raw_line), "%s %s", ip, hostname);
    else
        snprintf(e->raw_line, sizeof(e->raw_line), ";%s %s", ip, hostname);

    hf->entry_count++;
    hf->dirty = true;
    return true;
}
