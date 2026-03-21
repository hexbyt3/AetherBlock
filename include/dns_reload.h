#ifndef DNS_RELOAD_H
#define DNS_RELOAD_H

/**
 * @file dns_reload.h
 * @brief Interface for reloading the DNS-mitm hosts file at runtime.
 *
 * Provides a function to signal Atmosphere's dns_mitm module to re-read
 * the hosts file without requiring a full system reboot.
 */

#include <switch.h>

/** Reload the dns_mitm hosts file so changes take effect immediately. */
Result reloadDnsMitmHostsFile(void);

#endif
