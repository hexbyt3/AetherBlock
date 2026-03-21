#ifndef HOSTS_PARSER_H
#define HOSTS_PARSER_H

/**
 * @file hosts_parser.h
 * @brief Hosts-file parsing, editing, and persistence.
 *
 * Defines data structures for representing individual host entries and a
 * complete hosts file, along with functions for loading, saving, toggling,
 * and adding entries.
 */

#include <switch.h>
#include <stdbool.h>
#include "config.h"

/** Category tag used to group host entries by purpose. */
typedef enum {
    CATEGORY_TELEMETRY = 0, /**< Nintendo telemetry / analytics domains */
    CATEGORY_SYSTEM_UPDATES,/**< System firmware update servers */
    CATEGORY_GAME_CONTENT,  /**< Game-related content delivery */
    CATEGORY_ESHOP,         /**< eShop storefront domains */
    CATEGORY_OTHER,         /**< Uncategorised entries */
    CATEGORY_COUNT          /**< Sentinel value (total number of categories) */
} HostCategory;

/** A single parsed line from the hosts file. */
typedef struct {
    char ip[64];                        /**< Destination IP address (e.g. "0.0.0.0") */
    char hostname[192];                 /**< Hostname being redirected */
    char raw_line[HOSTS_MAX_LINE_LEN];  /**< Original line text for round-tripping */
    bool enabled;                       /**< true if the rule is active (not commented out) */
    bool is_comment;                    /**< true if the line is a pure comment */
    bool is_blank;                      /**< true if the line is blank / whitespace-only */
    HostCategory category;              /**< Inferred category for the hostname */
} HostEntry;

/** In-memory representation of an entire hosts file. */
typedef struct {
    HostEntry entries[HOSTS_MAX_ENTRIES]; /**< Array of parsed entries */
    int entry_count;                     /**< Number of valid entries */
    char active_path[256];               /**< Filesystem path the file was loaded from */
    bool dirty;                          /**< true if unsaved modifications exist */
} HostsFile;

/** Return a human-readable name for the given category. */
const char *categoryName(HostCategory cat);

/** Infer a HostCategory from a hostname string. */
HostCategory hostsInferCategory(const char *hostname);

/** Parse a single text line into a HostEntry. */
void hostsParseLine(HostEntry *e, const char *line);

/** Load and parse the hosts file from the active path into @p hf. */
Result hostsLoad(HostsFile *hf);

/** Write the current entries in @p hf back to the active path. */
Result hostsSave(HostsFile *hf);

/** Toggle the enabled/disabled state of the entry at @p index. */
void hostsToggleEntry(HostsFile *hf, int index);

/** Append a new entry to the hosts file. Returns true on success. */
bool hostsAddEntry(HostsFile *hf, const char *ip, const char *hostname, bool enabled, HostCategory cat);

#endif
