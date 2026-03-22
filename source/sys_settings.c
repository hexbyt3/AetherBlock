#include "sys_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* Local only -- nothing here is sent to Nintendo. Safe for sysnand online. */
SysSettingDef g_sys_setting_defs[SYS_SETTING_COUNT] = {
    /* Update suppression -- Nintendo system settings, overridden via set:sys mitm.
       Not in Atmosphere's hardcoded defaults but accepted by the generic INI parser. */
    { "ns.notification", "enable_network_update", "u8", "0x0", "0x1",
      "Block FW Update Check", "Suppress firmware update checking over network",
      SETTING_CAT_UPDATE_SUPPRESS, false, false, false },
    { "ns.notification", "enable_download_task_list", "u8", "0x0", "0x1",
      "Block Download Tasks",  "Suppress background download tasks (updates, etc.)",
      SETTING_CAT_UPDATE_SUPPRESS, false, false, false },
    { "ns.notification", "enable_version_list", "u8", "0x0", "0x1",
      "Block Version List",    "Suppress version list retrieval from Nintendo",
      SETTING_CAT_UPDATE_SUPPRESS, false, false, false },
    { "ns.notification", "enable_download_ticket", "u8", "0x0", "0x1",
      "Block Download Tickets","Suppress download ticket acquisition",
      SETTING_CAT_UPDATE_SUPPRESS, false, false, false },

    /* Network / DNS -- verified in settings_sd_kvs.cpp lines 369-378 */
    { "atmosphere", "enable_dns_mitm", "u8", "0x1", "0x0",
      "DNS MITM",              "Enable Atmosphere DNS interception (hosts file blocking)",
      SETTING_CAT_NETWORK, false, false, true },
    { "atmosphere", "add_defaults_to_dns_hosts", "u8", "0x1", "0x0",
      "Default DNS Hosts",     "Include Atmosphere's default telemetry blocks in DNS hosts",
      SETTING_CAT_NETWORK, false, false, true },
    { "atmosphere", "enable_dns_mitm_debug_log", "u8", "0x1", "0x0",
      "DNS Debug Log",         "Log DNS queries to SD card for debugging",
      SETTING_CAT_NETWORK, false, false, false },

    /* Telemetry / Error Reporting -- verified in settings_sd_kvs.cpp lines 311, 330 */
    { "eupld", "upload_enabled", "u8", "0x0", "0x1",
      "Block Error Uploads",   "Prevent error report uploads to Nintendo (default: blocked)",
      SETTING_CAT_TELEMETRY, false, false, true },
    { "erpt", "disable_automatic_report_cleanup", "u8", "0x1", "0x0",
      "Keep Error Reports",    "Preserve error reports on device instead of auto-cleaning",
      SETTING_CAT_TELEMETRY, false, false, false },

    /* Homebrew / System -- verified in settings_sd_kvs.cpp lines 348-365, 399 */
    { "atmosphere", "dmnt_cheats_enabled_by_default", "u8", "0x1", "0x0",
      "Cheats Enabled",        "Enable dmnt cheats by default when launching games",
      SETTING_CAT_HOMEBREW, false, false, true },
    { "atmosphere", "dmnt_always_save_cheat_toggles", "u8", "0x1", "0x0",
      "Save Cheat Toggles",    "Remember cheat toggle state between game launches",
      SETTING_CAT_HOMEBREW, false, false, false },
    { "atmosphere", "enable_am_debug_mode", "u8", "0x1", "0x0",
      "AM Debug Mode",         "Allow installing content without valid signatures",
      SETTING_CAT_HOMEBREW, false, false, false },
    { "atmosphere", "enable_external_bluetooth_db", "u8", "0x1", "0x0",
      "Shared Bluetooth DB",   "Share Bluetooth pairings across sysmmc and emummc",
      SETTING_CAT_HOMEBREW, false, false, false },
};

static const char *SETTING_CAT_NAMES[SETTING_CAT_COUNT] = {
    "Update Suppression",
    "Network",
    "Telemetry",
    "Homebrew",
};

const char *settingCategoryName(SettingCategory cat) {
    if (cat >= 0 && cat < SETTING_CAT_COUNT)
        return SETTING_CAT_NAMES[cat];
    return "Other";
}

static void trimRight(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' '))
        s[--len] = '\0';
}

static void ensureDir(const char *path) {
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *slash = strrchr(tmp, '/');
    if (!slash) return;
    *slash = '\0';

    mkdir(tmp, 0755);
}

Result sysSettingsLoad(SysSettingsFile *sf) {
    memset(sf, 0, sizeof(*sf));

    FILE *f = fopen(SYS_SETTINGS_PATH, "r");
    if (!f) {
        sf->file_existed = false;
        return 0;
    }

    sf->file_existed = true;
    char line[SYS_SETTINGS_MAX_LINE];
    while (fgets(line, sizeof(line), f) && sf->line_count < SYS_SETTINGS_MAX_LINES) {
        strncpy(sf->lines[sf->line_count], line, SYS_SETTINGS_MAX_LINE - 1);
        trimRight(sf->lines[sf->line_count]);
        sf->line_count++;
    }

    fclose(f);
    return 0;
}

static void sectionAtLine(SysSettingsFile *sf, int line_idx, char *out, int out_sz) {
    out[0] = '\0';

    for (int i = 0; i <= line_idx; i++) {
        const char *l = sf->lines[i];
        while (*l && isspace((unsigned char)*l)) l++;
        if (*l == '[') {
            l++;
            int j = 0;
            while (*l && *l != ']' && j < out_sz - 1)
                out[j++] = *l++;
            out[j] = '\0';
        }
    }
}

static bool parseKeyValue(const char *line, char *out_key, int key_sz, char *out_val, int val_sz) {
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p == '[' || *p == ';' || *p == '#' || *p == '\0')
        return false;

    int i = 0;
    while (*p && *p != '=' && !isspace((unsigned char)*p) && i < key_sz - 1)
        out_key[i++] = *p++;
    out_key[i] = '\0';

    while (*p && (*p == ' ' || *p == '=' || *p == '\t')) p++;

    const char *bang = strchr(p, '!');
    if (bang) p = bang + 1;

    while (*p && isspace((unsigned char)*p)) p++;

    i = 0;
    while (*p && !isspace((unsigned char)*p) && i < val_sz - 1)
        out_val[i++] = *p++;
    out_val[i] = '\0';

    return out_key[0] != '\0';
}

void sysSettingsReadStates(SysSettingsFile *sf) {
    for (int d = 0; d < SYS_SETTING_COUNT; d++) {
        SysSettingDef *def = &g_sys_setting_defs[d];
        def->found_in_file = false;
        def->toggled_on = def->is_default_on;
    }

    for (int i = 0; i < sf->line_count; i++) {
        char key[128], val[64];
        if (!parseKeyValue(sf->lines[i], key, sizeof(key), val, sizeof(val)))
            continue;

        char sec[128];
        sectionAtLine(sf, i, sec, sizeof(sec));

        for (int d = 0; d < SYS_SETTING_COUNT; d++) {
            SysSettingDef *def = &g_sys_setting_defs[d];
            if (strcmp(sec, def->section) == 0 && strcmp(key, def->key) == 0) {
                def->found_in_file = true;
                def->toggled_on = (strcmp(val, def->on_val) == 0);
                break;
            }
        }
    }
}

static int findKeyLine(SysSettingsFile *sf, const char *section, const char *key) {
    char cur_section[128] = {0};

    for (int i = 0; i < sf->line_count; i++) {
        const char *l = sf->lines[i];
        while (*l && isspace((unsigned char)*l)) l++;

        if (*l == '[') {
            l++;
            int j = 0;
            while (*l && *l != ']' && j < 126)
                cur_section[j++] = *l++;
            cur_section[j] = '\0';
            continue;
        }

        if (strcmp(cur_section, section) != 0)
            continue;

        char k[128], v[64];
        if (parseKeyValue(sf->lines[i], k, sizeof(k), v, sizeof(v))) {
            if (strcmp(k, key) == 0)
                return i;
        }
    }
    return -1;
}

static int findSectionLine(SysSettingsFile *sf, const char *section) {
    for (int i = 0; i < sf->line_count; i++) {
        const char *l = sf->lines[i];
        while (*l && isspace((unsigned char)*l)) l++;
        if (*l != '[') continue;
        l++;
        char sec[128];
        int j = 0;
        while (*l && *l != ']' && j < 126)
            sec[j++] = *l++;
        sec[j] = '\0';
        if (strcmp(sec, section) == 0)
            return i;
    }
    return -1;
}

static void insertLine(SysSettingsFile *sf, int at, const char *content) {
    if (sf->line_count >= SYS_SETTINGS_MAX_LINES) return;

    for (int i = sf->line_count; i > at; i--)
        memcpy(sf->lines[i], sf->lines[i - 1], SYS_SETTINGS_MAX_LINE);

    strncpy(sf->lines[at], content, SYS_SETTINGS_MAX_LINE - 1);
    sf->lines[at][SYS_SETTINGS_MAX_LINE - 1] = '\0';
    sf->line_count++;
}

void sysSettingsToggle(SysSettingsFile *sf, int index) {
    if (index < 0 || index >= SYS_SETTING_COUNT) return;

    SysSettingDef *def = &g_sys_setting_defs[index];
    def->toggled_on = !def->toggled_on;

    const char *new_val = def->toggled_on ? def->on_val : def->off_val;

    char new_line[SYS_SETTINGS_MAX_LINE];
    snprintf(new_line, sizeof(new_line), "%s = %s!%s", def->key, def->type, new_val);

    int line_idx = findKeyLine(sf, def->section, def->key);
    if (line_idx >= 0) {
        strncpy(sf->lines[line_idx], new_line, SYS_SETTINGS_MAX_LINE - 1);
    } else {
        if (sf->line_count > SYS_SETTINGS_MAX_LINES - 3)
            return;

        int sec_idx = findSectionLine(sf, def->section);
        if (sec_idx < 0) {
            if (sf->line_count > 0)
                insertLine(sf, sf->line_count, "");
            char sec_header[128];
            snprintf(sec_header, sizeof(sec_header), "[%s]", def->section);
            insertLine(sf, sf->line_count, sec_header);
            sec_idx = sf->line_count - 1;
        }
        insertLine(sf, sec_idx + 1, new_line);
        def->found_in_file = true;
    }

    sf->dirty = true;
}

Result sysSettingsSave(SysSettingsFile *sf) {
    ensureDir(SYS_SETTINGS_PATH);

    char tmp_path[280];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", SYS_SETTINGS_PATH);

    FILE *f = fopen(tmp_path, "w");
    if (!f) return MAKERESULT(Module_Libnx, 1);

    for (int i = 0; i < sf->line_count; i++)
        fprintf(f, "%s\n", sf->lines[i]);

    fflush(f);
    fclose(f);

    remove(SYS_SETTINGS_PATH);
    if (rename(tmp_path, SYS_SETTINGS_PATH) != 0)
        return MAKERESULT(Module_Libnx, 2);

    sf->dirty = false;
    sf->file_existed = true;
    return 0;
}
