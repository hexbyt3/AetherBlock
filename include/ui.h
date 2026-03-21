#ifndef UI_H
#define UI_H

/**
 * @file ui.h
 * @brief SDL2-based user interface for AetherBlock.
 *
 * Manages the graphical interface including screen rendering, navigation
 * state, toast notifications, and the network-test overlay.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include "config.h"
#include "hosts_parser.h"
#include "profiles.h"
#include "net_test.h"

/** Enumeration of application screens / views. */
typedef enum {
    SCREEN_MAIN_LIST = 0, /**< Primary hosts-entry list view */
    SCREEN_PROFILES,      /**< Profile-preset selection screen */
    SCREEN_CONFIRM,       /**< Confirmation dialog */
    SCREEN_STATUS,        /**< Status / information screen */
    SCREEN_NET_TEST,      /**< Network connectivity test screen */
} AppScreen;

/** Complete UI state including SDL handles, navigation, and overlays. */
typedef struct {
    SDL_Window   *window;          /**< Main application window */
    SDL_Renderer *renderer;        /**< SDL renderer for the window */
    TTF_Font     *font_large;      /**< Large font (headings) */
    TTF_Font     *font_normal;     /**< Normal font (list items) */
    TTF_Font     *font_small;      /**< Small font (hints, secondary text) */
    TTF_Font     *font_title;      /**< Title-bar font */
    int           scroll_offset;   /**< Vertical scroll position in the list */
    int           cursor_index;    /**< Currently highlighted list index */
    AppScreen     current_screen;  /**< Active screen / view */
    char          status_msg[256]; /**< Text shown on the status screen */
    int           status_msg_timer;/**< Remaining frames to display status */
    int           confirm_action;  /**< Pending action for the confirm dialog */
    int           profile_cursor;  /**< Selected index on the profiles screen */

    /* Toast notification (overlay on main screen) */
    char          toast_msg[256];  /**< Toast message text */
    int           toast_timer;     /**< Remaining frames for the toast */
    SDL_Color     toast_color;     /**< Background colour of the toast */

    /* Net test state */
    NetTestState  net_test;        /**< Current network-test state */
    int           net_test_scroll; /**< Scroll offset for the net-test list */
} UIState;

/** Initialise the UI subsystem (SDL, fonts, default state). Returns true on success. */
bool uiInit(UIState *ui);

/** Tear down the UI and release all SDL resources. */
void uiDestroy(UIState *ui);

/** Render a single frame of the UI using the current hosts-file data. */
void uiRender(UIState *ui, HostsFile *hf);

/** Display a temporary toast notification with the given colour. */
void uiShowToast(UIState *ui, const char *msg, SDL_Color color);

#endif
