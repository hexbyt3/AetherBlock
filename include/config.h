#ifndef CONFIG_H
#define CONFIG_H

/**
 * @file config.h
 * @brief Application-wide constants and configuration defaults.
 *
 * Centralises compile-time settings such as version information, hosts-file
 * limits, screen dimensions, UI layout metrics, and timing constants.
 */

/** @name Application metadata */
/** @{ */
#define APP_NAME            "AetherBlock"
#define APP_VERSION         "1.1.0"
#define APP_AUTHOR          "HeXbyt3"
/** @} */

/** @name Hosts-file size limits */
/** @{ */
#define HOSTS_MAX_ENTRIES   256
#define HOSTS_MAX_LINE_LEN  256
#define HOSTS_MAX_FILE_SIZE 32768
/** @} */

/** @name Hosts-file filesystem paths */
/** @{ */
#define HOSTS_PATH_DEFAULT  "/atmosphere/hosts/default.txt"
#define HOSTS_PATH_SYSMMC   "/atmosphere/hosts/sysmmc.txt"
#define HOSTS_PATH_EMUMMC   "/atmosphere/hosts/emummc.txt"
/** @} */

/** @name Screen dimensions */
/** @{ */
#define SCREEN_WIDTH        1280
#define SCREEN_HEIGHT       720
/** @} */

/** @name UI layout metrics */
/** @{ */
#define ROW_HEIGHT          40
#define VISIBLE_ROWS        14
#define TITLE_BAR_HEIGHT    70
#define HINT_BAR_HEIGHT     56
#define LIST_TOP_Y          (TITLE_BAR_HEIGHT + 2)
#define LIST_BOTTOM_Y       (SCREEN_HEIGHT - HINT_BAR_HEIGHT)
/** @} */

/** @name Input timing */
/** @{ */
#define KEY_REPEAT_FIRST_MS 400
#define KEY_REPEAT_MS       80
/** @} */

/** @name Notification display durations (in frames) */
/** @{ */
#define STATUS_MSG_FRAMES   180
#define TOAST_FRAMES        120
/** @} */

#endif
