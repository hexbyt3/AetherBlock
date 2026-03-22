#ifndef CONFIG_H
#define CONFIG_H

#define APP_NAME            "AetherBlock"
#define APP_VERSION         "1.0.1"
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

#endif
