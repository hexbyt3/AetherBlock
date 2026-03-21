#ifndef PROFILES_H
#define PROFILES_H

/**
 * @file profiles.h
 * @brief Predefined blocking-profile presets.
 *
 * Provides a set of curated profiles that configure the hosts file to
 * different blocking levels, from full block-all to fully permissive.
 */

#include "hosts_parser.h"

/** Available blocking-profile presets. */
typedef enum {
    PROFILE_BLOCK_ALL = 0,           /**< Block all known Nintendo domains */
    PROFILE_BLOCK_FW_AND_TELEMETRY,  /**< Block firmware updates and telemetry only */
    PROFILE_TELEMETRY_ONLY,          /**< Block telemetry / analytics only */
    PROFILE_ALLOW_ALL,               /**< Allow all connections (no blocking) */
    PROFILE_COUNT                    /**< Sentinel value (total number of profiles) */
} ProfilePreset;

/** Static definition of a profile (name + description). */
typedef struct {
    const char *name;        /**< Short display name */
    const char *description; /**< One-line explanation shown in the UI */
} ProfileDef;

/** Array of profile definitions, indexed by ProfilePreset. */
extern const ProfileDef PROFILE_DEFS[PROFILE_COUNT];

/** Apply the given preset to the hosts file, enabling/disabling entries accordingly. */
void profileApply(HostsFile *hf, ProfilePreset preset);

/** Seed a hosts file with the default set of known Nintendo domains. */
void profileSeedDefaults(HostsFile *hf);

#endif
