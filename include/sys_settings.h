#ifndef SYS_SETTINGS_H
#define SYS_SETTINGS_H

#include <switch.h>
#include <stdbool.h>

#define SYS_SETTINGS_PATH       "/atmosphere/config/system_settings.ini"
#define SYS_SETTINGS_MAX_LINE   512
#define SYS_SETTINGS_MAX_LINES  512

typedef enum {
    SETTING_CAT_NETWORK = 0,
    SETTING_CAT_TELEMETRY,
    SETTING_CAT_HOMEBREW,
    SETTING_CAT_COUNT,
} SettingCategory;

typedef struct {
    const char *section;
    const char *key;
    const char *type;
    const char *on_val;        /* value when toggle is ON */
    const char *off_val;       /* value when toggle is OFF */
    const char *display_name;
    const char *description;
    SettingCategory category;
    bool toggled_on;           /* current state: true = on_val written */
    bool found_in_file;
    bool is_default_on;        /* what Atmosphere defaults to */
} SysSettingDef;

typedef struct {
    char lines[SYS_SETTINGS_MAX_LINES][SYS_SETTINGS_MAX_LINE];
    int  line_count;
    bool dirty;
    bool file_existed;
} SysSettingsFile;

#define SYS_SETTING_COUNT 9

extern SysSettingDef g_sys_setting_defs[SYS_SETTING_COUNT];

const char *settingCategoryName(SettingCategory cat);

Result sysSettingsLoad(SysSettingsFile *sf);
Result sysSettingsSave(SysSettingsFile *sf);
void   sysSettingsReadStates(SysSettingsFile *sf);
void   sysSettingsToggle(SysSettingsFile *sf, int index);

#endif
