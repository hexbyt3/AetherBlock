#ifndef CONFIG_H
#define CONFIG_H

#define APP_NAME            "AetherBlock"
#define APP_VERSION         "2.0.4"
#define APP_AUTHOR          "HeXbyt3"

#define HOSTS_MAX_ENTRIES   256
#define HOSTS_MAX_LINE_LEN  256
#define HOSTS_MAX_FILE_SIZE 32768

#define HOSTS_PATH_DEFAULT  "/atmosphere/hosts/default.txt"
#define HOSTS_PATH_SYSMMC   "/atmosphere/hosts/sysmmc.txt"
#define HOSTS_PATH_EMUMMC   "/atmosphere/hosts/emummc.txt"

#define SCREEN_WIDTH        1280
#define SCREEN_HEIGHT       720

#define ROW_HEIGHT          40
#define VISIBLE_ROWS        14
#define TITLE_BAR_HEIGHT    70
#define HINT_BAR_HEIGHT     56
#define LIST_TOP_Y          (TITLE_BAR_HEIGHT + 2)
#define LIST_BOTTOM_Y       (SCREEN_HEIGHT - HINT_BAR_HEIGHT)

#define KEY_REPEAT_FIRST_MS 400
#define KEY_REPEAT_MS       80

#define STATUS_MSG_FRAMES   180
#define TOAST_FRAMES        120

/* download / update paths */
#define FIRMWARE_RELEASES_URL  "https://api.github.com/repos/THZoria/NX_Firmware/releases?per_page=30"
#define FIRMWARE_DOWNLOAD_PATH "/config/AetherBlock/firmware.zip"
#define FIRMWARE_EXTRACT_PATH  "/firmware/"
#define CFW_DOWNLOAD_PATH      "/config/AetherBlock/cfw_package.zip"
#define CFW_API_URL            "https://api.github.com/repos/hexbyt3/CFW4SysBots/releases/latest"
#define CFW_ASSET_MARIKO       "mod-chipped"
#define CFW_ASSET_ERISTA       "unpatched"
#define DAYBREAK_PATH          "/switch/daybreak.nro"
#define AETHERBLOCK_CONFIG_DIR "/config/AetherBlock/"

#endif
