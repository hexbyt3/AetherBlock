#include "ams_bpc.h"

/* bpc:ams is a named port exposed by Atmosphère's ams_mitm. We talk to it
   directly via the kernel (svcConnectToNamedPort) rather than through sm --
   which is why callers must smExit() before amsBpcInitialize(). Command 65001
   is SetRebootPayload; it copies the supplied buffer into the live reboot
   buffer and forces the next reboot to be a payload reboot. */

static Service g_amsBpcSrv;
static u64     g_refCnt;

Result amsBpcInitialize(void) {
    if (g_refCnt > 0) {
        g_refCnt++;
        return 0;
    }

    Handle h;
    Result rc = svcConnectToNamedPort(&h, "bpc:ams");
    if (R_SUCCEEDED(rc)) {
        serviceCreate(&g_amsBpcSrv, h);
        g_refCnt++;
    }
    return rc;
}

void amsBpcExit(void) {
    if (g_refCnt == 0)
        return;
    if (--g_refCnt == 0)
        serviceClose(&g_amsBpcSrv);
}

Result amsBpcSetRebootPayload(const void *src, size_t src_size) {
    return serviceDispatch(&g_amsBpcSrv, 65001,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
        .buffers = { { src, src_size } },
    );
}
