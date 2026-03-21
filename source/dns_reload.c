#include "dns_reload.h"

Result reloadDnsMitmHostsFile(void) {
    Result rc;
    Service sfdnsresSrv;

    rc = smGetService(&sfdnsresSrv, "sfdnsres");
    if (R_FAILED(rc)) return rc;

    /* cmd 65000 tells atmosphere's dns_mitm to re-read the hosts file */
    rc = serviceDispatch(&sfdnsresSrv, 65000);
    serviceClose(&sfdnsresSrv);

    return rc;
}
