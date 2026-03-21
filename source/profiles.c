#include "profiles.h"
#include <string.h>

const ProfileDef PROFILE_DEFS[PROFILE_COUNT] = {
    { "Block All",              "Block all Nintendo servers (telemetry, updates, game content)" },
    { "Allow Game Updates",     "Block firmware updates & telemetry, allow game updates" },
    { "Telemetry Only",         "Only block telemetry, allow everything else" },
    { "Allow All",              "Unblock everything (disable all host redirects)" },
};

typedef struct {
    const char *ip;
    const char *hostname;
    HostCategory category;
} CatalogEntry;

static const CatalogEntry CATALOG[] = {
    /* telemetry */
    { "127.0.0.1", "receive-lp1.dg.srv.nintendo.net",          CATEGORY_TELEMETRY },
    { "127.0.0.1", "receive-lp1.er.srv.nintendo.net",          CATEGORY_TELEMETRY },

    /* fw updates */
    { "127.0.0.1", "sun.hac.lp1.d4c.nintendo.net",             CATEGORY_SYSTEM_UPDATES },
    { "127.0.0.1", "atumn.hac.lp1.d4c.nintendo.net",           CATEGORY_SYSTEM_UPDATES },

    /* game content */
    { "127.0.0.1", "superfly.hac.lp1.d4c.nintendo.net",        CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "aqua.hac.lp1.d4c.nintendo.net",            CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "tagaya.hac.lp1.eshop.nintendo.net",        CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "beach.hac.lp1.eshop.nintendo.net",         CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "pearljam.hac.lp1.eshop.nintendo.net",      CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "pushmo.hac.lp1.eshop.nintendo.net",        CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "veer.hac.lp1.d4c.nintendo.net",            CATEGORY_GAME_CONTENT },

    /* game content - cdn */
    { "127.0.0.1", "atum.hac.lp1.d4c.nintendo.net",            CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "atum-4ff.hac.lp1.d4c.nintendo.net",        CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "atum-eda.hac.lp1.d4c.nintendo.net",        CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "atum-5a8.hac.lp1.d4c.nintendo.net",        CATEGORY_GAME_CONTENT },
    { "127.0.0.1", "atum-f03.hac.lp1.d4c.nintendo.net",        CATEGORY_GAME_CONTENT },

    /* eshop */
    { "127.0.0.1", "ecs-lp1.hac.shop.nintendo.net",            CATEGORY_ESHOP },
    { "127.0.0.1", "ias-lp1.hac.shop.nintendo.net",            CATEGORY_ESHOP },
    { "127.0.0.1", "bugyo.hac.lp1.eshop.nintendo.net",         CATEGORY_ESHOP },
};

#define CATALOG_COUNT (sizeof(CATALOG) / sizeof(CATALOG[0]))

void profileSeedDefaults(HostsFile *hf) {
    for (size_t i = 0; i < CATALOG_COUNT; i++) {
        hostsAddEntry(hf, CATALOG[i].ip, CATALOG[i].hostname, true, CATALOG[i].category);
    }
}

void profileApply(HostsFile *hf, ProfilePreset preset) {
    profileSeedDefaults(hf);

    for (int i = 0; i < hf->entry_count; i++) {
        HostEntry *e = &hf->entries[i];
        if (e->is_comment || e->is_blank) continue;

        switch (preset) {
        case PROFILE_BLOCK_ALL:
            e->enabled = true;
            break;

        case PROFILE_BLOCK_FW_AND_TELEMETRY:
            if (e->category == CATEGORY_TELEMETRY || e->category == CATEGORY_SYSTEM_UPDATES)
                e->enabled = true;
            else
                e->enabled = false;
            break;

        case PROFILE_TELEMETRY_ONLY:
            if (e->category == CATEGORY_TELEMETRY)
                e->enabled = true;
            else
                e->enabled = false;
            break;

        case PROFILE_ALLOW_ALL:
            e->enabled = false;
            break;

        default:
            break;
        }
    }

    hf->dirty = true;
}
