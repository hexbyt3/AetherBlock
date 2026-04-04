#include "cfw_detect.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>

#define SPL_EXOSPHERE_API_VERSION 65000
#define SPL_HARDWARE_TYPE         5

int detectCfwInfo(CfwInfo *info) {
    memset(info, 0, sizeof(*info));

    snprintf(info->fw_version, sizeof(info->fw_version), "Unknown");
    snprintf(info->ams_version, sizeof(info->ams_version), "Unknown");

    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            snprintf(info->fw_version, sizeof(info->fw_version),
                     "%u.%u.%u", fw.major, fw.minor, fw.micro);
        setsysExit();
    }

    if (R_SUCCEEDED(splInitialize())) {
        u64 exo_ver = 0;
        if (R_SUCCEEDED(splGetConfig(SPL_EXOSPHERE_API_VERSION, &exo_ver))) {
            u32 major = (exo_ver >> 56) & 0xFF;
            u32 minor = (exo_ver >> 48) & 0xFF;
            u32 micro = (exo_ver >> 40) & 0xFF;
            snprintf(info->ams_version, sizeof(info->ams_version),
                     "%u.%u.%u", major, minor, micro);
        }

        /* 0=Icosa(dev), 1=Copper(Erista retail), 2+=Mariko variants */
        u64 hw_type = 0;
        if (R_SUCCEEDED(splGetConfig(SPL_HARDWARE_TYPE, &hw_type)))
            info->is_mariko = hw_type >= 2;

        splExit();
    }

    return 0;
}
