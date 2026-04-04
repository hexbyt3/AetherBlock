#ifndef CFW_DETECT_H
#define CFW_DETECT_H

#include <stdbool.h>

typedef struct {
    char ams_version[32];
    char fw_version[32];
    bool is_mariko;
} CfwInfo;

int detectCfwInfo(CfwInfo *info);

#endif
