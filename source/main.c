#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "hosts_parser.h"
#include "dns_reload.h"
#include "profiles.h"
#include "input.h"
#include "ui.h"
#include "net_test.h"
#include "sys_settings.h"
#include "download.h"
#include "cfw_detect.h"
#include "firmware_mgr.h"
#include "cfw_mgr.h"
#include "pending.h"

static HostsFile       s_hosts;
static UIState         s_ui;
static SysSettingsFile s_sys_settings;
static bool            s_exit_requested;

static const SDL_Color TOAST_SUCCESS = { 50, 190,  70, 255};
static const SDL_Color TOAST_WARN    = {230, 185,  40, 255};
static const SDL_Color TOAST_ERROR   = {230,  55,  55, 255};
static const SDL_Color TOAST_INFO    = { 70, 135, 220, 255};

static void ensureCursorVisible(void) {
    int max_visible = (LIST_BOTTOM_Y - LIST_TOP_Y - 8) / ROW_HEIGHT;

    if (s_ui.cursor_index < s_ui.scroll_offset)
        s_ui.scroll_offset = s_ui.cursor_index;
    else if (s_ui.cursor_index >= s_ui.scroll_offset + max_visible)
        s_ui.scroll_offset = s_ui.cursor_index - max_visible + 1;

    if (s_ui.scroll_offset < 0)
        s_ui.scroll_offset = 0;
}

static void clampCursor(void) {
    if (s_hosts.entry_count == 0) {
        s_ui.cursor_index = 0;
        return;
    }
    if (s_ui.cursor_index < 0)
        s_ui.cursor_index = 0;
    if (s_ui.cursor_index >= s_hosts.entry_count)
        s_ui.cursor_index = s_hosts.entry_count - 1;
}

static int countHostEntries(void) {
    int n = 0;
    for (int i = 0; i < s_hosts.entry_count; i++) {
        if (!s_hosts.entries[i].is_comment && !s_hosts.entries[i].is_blank)
            n++;
    }
    return n;
}

static void doSaveAndReload(void) {
    Result rc = hostsSave(&s_hosts);
    if (R_FAILED(rc)) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Save failed! (rc=0x%X)", rc);
        uiShowToast(&s_ui, buf, TOAST_ERROR);
        return;
    }

    rc = reloadDnsMitmHostsFile();
    if (R_FAILED(rc)) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Saved, but DNS reload failed (rc=0x%X)", rc);
        uiShowToast(&s_ui, buf, TOAST_WARN);
        return;
    }

    uiShowToast(&s_ui, "Saved and reloaded DNS hosts successfully!", TOAST_SUCCESS);
}

static void handleMainList(InputState *input) {
    int max_visible = (LIST_BOTTOM_Y - LIST_TOP_Y - 8) / ROW_HEIGHT;

    for (int i = 0; i < input->count; i++) {
        switch (input->events[i]) {
        case INPUT_UP:
            s_ui.cursor_index--;
            clampCursor();
            ensureCursorVisible();
            break;

        case INPUT_DOWN:
            s_ui.cursor_index++;
            clampCursor();
            ensureCursorVisible();
            break;

        case INPUT_LEFT:
            s_ui.cursor_index -= max_visible;
            clampCursor();
            ensureCursorVisible();
            break;

        case INPUT_RIGHT:
            s_ui.cursor_index += max_visible;
            clampCursor();
            ensureCursorVisible();
            break;

        case INPUT_A:
            hostsToggleEntry(&s_hosts, s_ui.cursor_index);
            break;

        case INPUT_Y:
            if (s_hosts.dirty)
                doSaveAndReload();
            else
                uiShowToast(&s_ui, "No changes to save.", TOAST_INFO);
            break;

        case INPUT_X:
            s_ui.current_screen = SCREEN_PROFILES;
            s_ui.profile_cursor = 0;
            break;

        case INPUT_L:
            if (countHostEntries() > 0) {
                netTestPrepare(&s_ui.net_test, &s_hosts);
                s_ui.net_test_scroll = 0;
                s_ui.current_screen = SCREEN_NET_TEST;
            } else {
                uiShowToast(&s_ui, "No entries to test. Seed defaults first.", TOAST_WARN);
            }
            break;

        case INPUT_R:
            s_ui.sys_settings_cursor = 0;
            s_ui.sys_settings_scroll = 0;
            s_ui.current_screen = SCREEN_SYS_SETTINGS;
            break;

        case INPUT_ZL:
            s_ui.current_screen = SCREEN_FW_MANAGER;
            break;

        case INPUT_ZR:
            s_ui.current_screen = SCREEN_CFW_MANAGER;
            break;

        case INPUT_MINUS:
            profileSeedDefaults(&s_hosts);
            if (s_hosts.dirty)
                uiShowToast(&s_ui, "Default entries added! Press Y to save.", TOAST_SUCCESS);
            else
                uiShowToast(&s_ui, "All default entries already present.", TOAST_INFO);
            break;

        case INPUT_PLUS:
            if (s_hosts.dirty)
                s_ui.current_screen = SCREEN_CONFIRM;
            else
                return;
            break;

        default:
            break;
        }
    }
}

static void handleProfiles(InputState *input) {
    for (int i = 0; i < input->count; i++) {
        switch (input->events[i]) {
        case INPUT_UP:
            if (s_ui.profile_cursor > 0)
                s_ui.profile_cursor--;
            break;

        case INPUT_DOWN:
            if (s_ui.profile_cursor < PROFILE_COUNT - 1)
                s_ui.profile_cursor++;
            break;

        case INPUT_A:
            profileApply(&s_hosts, (ProfilePreset)s_ui.profile_cursor);
            s_ui.current_screen = SCREEN_MAIN_LIST;
            uiShowToast(&s_ui, "Profile applied! Press Y to save & reload.", TOAST_SUCCESS);
            break;

        case INPUT_B:
            s_ui.current_screen = SCREEN_MAIN_LIST;
            break;

        default:
            break;
        }
    }
}

static void handleStatus(InputState *input) {
    s_ui.status_msg_timer--;

    if (input->count > 0 || s_ui.status_msg_timer <= 0)
        s_ui.current_screen = SCREEN_MAIN_LIST;
}

static void handleConfirm(InputState *input) {
    for (int i = 0; i < input->count; i++) {
        if (input->events[i] == INPUT_B)
            s_ui.current_screen = SCREEN_MAIN_LIST;
    }
}

static void handleNetTest(InputState *input) {
    if (s_ui.net_test.running)
        netTestStep(&s_ui.net_test);

    int row_h = 34;
    int max_visible = (LIST_BOTTOM_Y - TITLE_BAR_HEIGHT - 16) / row_h;

    for (int i = 0; i < input->count; i++) {
        switch (input->events[i]) {
        case INPUT_B:
            s_ui.net_test.running = false;
            s_ui.current_screen = SCREEN_MAIN_LIST;
            break;

        case INPUT_A:
            if (s_ui.net_test.finished) {
                netTestPrepare(&s_ui.net_test, &s_hosts);
                s_ui.net_test_scroll = 0;
            }
            break;

        case INPUT_UP:
            if (s_ui.net_test_scroll > 0)
                s_ui.net_test_scroll--;
            break;

        case INPUT_DOWN:
            if (s_ui.net_test_scroll < s_ui.net_test.count - max_visible)
                s_ui.net_test_scroll++;
            if (s_ui.net_test_scroll < 0)
                s_ui.net_test_scroll = 0;
            break;

        default:
            break;
        }
    }

    if (s_ui.net_test.running && s_ui.net_test.current >= s_ui.net_test_scroll + max_visible) {
        s_ui.net_test_scroll = s_ui.net_test.current - max_visible + 1;
    }
}

static void handleSysSettings(InputState *input) {
    for (int i = 0; i < input->count; i++) {
        switch (input->events[i]) {
        case INPUT_UP:
            if (s_ui.sys_settings_cursor > 0)
                s_ui.sys_settings_cursor--;
            break;

        case INPUT_DOWN:
            if (s_ui.sys_settings_cursor < SYS_SETTING_COUNT - 1)
                s_ui.sys_settings_cursor++;
            break;

        case INPUT_A:
            sysSettingsToggle(&s_sys_settings, s_ui.sys_settings_cursor);
            break;

        case INPUT_Y:
            if (s_sys_settings.dirty) {
                Result rc = sysSettingsSave(&s_sys_settings);
                if (R_SUCCEEDED(rc))
                    uiShowToast(&s_ui, "Settings saved! Reboot required for changes.", TOAST_SUCCESS);
                else {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Save failed! (rc=0x%X)", rc);
                    uiShowToast(&s_ui, buf, TOAST_ERROR);
                }
            } else {
                uiShowToast(&s_ui, "No changes to save.", TOAST_INFO);
            }
            break;

        case INPUT_B:
            s_ui.current_screen = SCREEN_MAIN_LIST;
            break;

        default:
            break;
        }
    }
}

static void handleFwManager(InputState *input) {
    FirmwareManager *fm = &s_ui.fw_mgr;

    if (fm->worker_active)
        return;

    for (int i = 0; i < input->count; i++) {
        switch (input->events[i]) {
        case INPUT_A:
            if (fm->state == FW_STATE_IDLE) {
                fwMgrStartFetch(fm);
            } else if (fm->state == FW_STATE_READY) {
                fwMgrStartDownload(fm);
            } else if (fm->state == FW_STATE_DONE) {
                if (fwMgrLaunchDaybreak() == 0) {
                    s_hosts.dirty = false;
                    s_exit_requested = true;
                    return;
                } else {
                    uiShowToast(&s_ui, "Daybreak not found at /switch/daybreak.nro", TOAST_ERROR);
                }
            } else if (fm->state == FW_STATE_ERROR) {
                fm->state = FW_STATE_IDLE;
            }
            break;

        case INPUT_UP:
            if (fm->state == FW_STATE_READY && fm->selected > 0)
                fm->selected--;
            break;

        case INPUT_DOWN:
            if (fm->state == FW_STATE_READY && fm->selected < fm->count - 1)
                fm->selected++;
            break;

        case INPUT_B:
            if (fm->state == FW_STATE_ERROR)
                fm->state = FW_STATE_IDLE;
            s_ui.current_screen = SCREEN_MAIN_LIST;
            break;

        default:
            break;
        }
    }
}

static void handleCfwManager(InputState *input) {
    CfwPackageManager *cm = &s_ui.cfw_mgr;

    if (cm->worker_active)
        return;

    for (int i = 0; i < input->count; i++) {
        switch (input->events[i]) {
        case INPUT_A:
            if (cm->state == CFW_STATE_IDLE) {
                cfwMgrStartFetch(cm);
            } else if (cm->state == CFW_STATE_READY) {
                cfwMgrStartDownload(cm);
            } else if (cm->state == CFW_STATE_DONE) {
                /* jump straight to the firmware manager so the user can
                   update firmware in the same session — the single reboot
                   at the end applies both. */
                s_ui.current_screen = SCREEN_FW_MANAGER;
            } else if (cm->state == CFW_STATE_ERROR) {
                cm->state = CFW_STATE_IDLE;
            }
            break;

        case INPUT_B:
            if (cm->state == CFW_STATE_ERROR)
                cm->state = CFW_STATE_IDLE;
            s_ui.current_screen = SCREEN_MAIN_LIST;
            break;

        default:
            break;
        }
    }
}

static bool shouldQuit(InputState *input) {
    if (s_exit_requested) return true;
    for (int i = 0; i < input->count; i++) {
        if (input->events[i] == INPUT_PLUS && s_ui.current_screen == SCREEN_MAIN_LIST && !s_hosts.dirty)
            return true;
        if (input->events[i] == INPUT_A && s_ui.current_screen == SCREEN_CONFIRM)
            return true;
    }
    return false;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    romfsInit();
    plInitialize(PlServiceType_User);
    socketInitializeDefault();

    /* finish any CFW file swaps that were staged before the last reboot
       (package3, stratosphere.romfs, AetherBlock.nro, etc.) */
    pendingApply();

    /* wipe staged /firmware/ dir if we handed off to Daybreak last run */
    fwMgrCleanupIfPending();

    downloadGlobalInit();

    hostsLoad(&s_hosts);

    sysSettingsLoad(&s_sys_settings);
    sysSettingsReadStates(&s_sys_settings);

    if (!uiInit(&s_ui)) {
        downloadGlobalCleanup();
        plExit();
        romfsExit();
        socketExit();
        return 1;
    }

    s_ui.sys_settings_file = &s_sys_settings;

    CfwInfo cfw;
    detectCfwInfo(&cfw);
    snprintf(s_ui.current_fw_version, sizeof(s_ui.current_fw_version), "%s", cfw.fw_version);

    fwMgrInit(&s_ui.fw_mgr, cfw.fw_version);
    cfwMgrInit(&s_ui.cfw_mgr);

    inputInit();

    while (appletMainLoop()) {
        InputState input;
        inputPoll(&input);

        if (shouldQuit(&input))
            break;

        switch (s_ui.current_screen) {
        case SCREEN_MAIN_LIST:
            handleMainList(&input);
            break;
        case SCREEN_PROFILES:
            handleProfiles(&input);
            break;
        case SCREEN_STATUS:
            handleStatus(&input);
            break;
        case SCREEN_CONFIRM:
            handleConfirm(&input);
            break;
        case SCREEN_NET_TEST:
            handleNetTest(&input);
            break;
        case SCREEN_SYS_SETTINGS:
            handleSysSettings(&input);
            break;
        case SCREEN_FW_MANAGER:
            handleFwManager(&input);
            break;
        case SCREEN_CFW_MANAGER:
            handleCfwManager(&input);
            break;
        }

        uiRender(&s_ui, &s_hosts);
    }

    uiDestroy(&s_ui);
    downloadGlobalCleanup();
    socketExit();
    plExit();
    romfsExit();

    return 0;
}
