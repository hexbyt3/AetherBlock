#ifndef UI_H
#define UI_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include "config.h"
#include "hosts_parser.h"
#include "profiles.h"
#include "net_test.h"

typedef enum {
    SCREEN_MAIN_LIST = 0,
    SCREEN_PROFILES,
    SCREEN_CONFIRM,
    SCREEN_STATUS,
    SCREEN_NET_TEST,
} AppScreen;

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    TTF_Font     *font_large;
    TTF_Font     *font_normal;
    TTF_Font     *font_small;
    TTF_Font     *font_title;
    int           scroll_offset;
    int           cursor_index;
    AppScreen     current_screen;
    char          status_msg[256];
    int           status_msg_timer;
    int           confirm_action;
    int           profile_cursor;

    char          toast_msg[256];
    int           toast_timer;
    SDL_Color     toast_color;

    NetTestState  net_test;
    int           net_test_scroll;
} UIState;

bool uiInit(UIState *ui);
void uiDestroy(UIState *ui);
void uiRender(UIState *ui, HostsFile *hf);
void uiShowToast(UIState *ui, const char *msg, SDL_Color color);

#endif
