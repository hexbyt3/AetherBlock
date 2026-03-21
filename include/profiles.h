#ifndef PROFILES_H
#define PROFILES_H

#include "hosts_parser.h"

typedef enum {
    PROFILE_BLOCK_ALL = 0,
    PROFILE_BLOCK_FW_AND_TELEMETRY,
    PROFILE_TELEMETRY_ONLY,
    PROFILE_ALLOW_ALL,
    PROFILE_COUNT
} ProfilePreset;

typedef struct {
    const char *name;
    const char *description;
} ProfileDef;

extern const ProfileDef PROFILE_DEFS[PROFILE_COUNT];

void profileApply(HostsFile *hf, ProfilePreset preset);
void profileSeedDefaults(HostsFile *hf);

#endif
