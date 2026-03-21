#ifndef HOSTS_PARSER_H
#define HOSTS_PARSER_H

#include <switch.h>
#include <stdbool.h>
#include "config.h"

typedef enum {
    CATEGORY_TELEMETRY = 0,
    CATEGORY_SYSTEM_UPDATES,
    CATEGORY_GAME_CONTENT,
    CATEGORY_ESHOP,
    CATEGORY_OTHER,
    CATEGORY_COUNT
} HostCategory;

typedef struct {
    char ip[64];
    char hostname[192];
    char raw_line[HOSTS_MAX_LINE_LEN];
    bool enabled;
    bool is_comment;
    bool is_blank;
    HostCategory category;
} HostEntry;

typedef struct {
    HostEntry entries[HOSTS_MAX_ENTRIES];
    int entry_count;
    char active_path[256];
    bool dirty;
} HostsFile;

const char *categoryName(HostCategory cat);
HostCategory hostsInferCategory(const char *hostname);
void hostsParseLine(HostEntry *e, const char *line);
Result hostsLoad(HostsFile *hf);
Result hostsSave(HostsFile *hf);
void hostsToggleEntry(HostsFile *hf, int index);
bool hostsAddEntry(HostsFile *hf, const char *ip, const char *hostname, bool enabled, HostCategory cat);

#endif
