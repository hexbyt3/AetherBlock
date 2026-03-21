/**
 * @file dns_reload.c
 * @brief Triggers a DNS MITM hosts file reload via the sfdnsres service.
 */

#include "dns_reload.h"

/**
 * @brief Request the system DNS resolver to reload its hosts file.
 *
 * Opens the sfdnsres service and dispatches command 65000, which signals
 * Atmosphere's DNS MITM module to re-read the hosts file from disk.
 *
 * @return Result code; 0 on success.
 */
Result reloadDnsMitmHostsFile(void) {
    Result rc;
    Service sfdnsresSrv;

    rc = smGetService(&sfdnsresSrv, "sfdnsres");
    if (R_FAILED(rc)) return rc;

    rc = serviceDispatch(&sfdnsresSrv, 65000);
    serviceClose(&sfdnsresSrv);

    return rc;
}
