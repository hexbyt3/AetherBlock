#ifndef INPUT_H
#define INPUT_H

#include <switch.h>
#include <stdbool.h>
#include "config.h"

typedef enum {
    INPUT_NONE = 0,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_A,
    INPUT_B,
    INPUT_X,
    INPUT_Y,
    INPUT_PLUS,
    INPUT_MINUS,
    INPUT_L,
    INPUT_R,
    INPUT_ZL,
    INPUT_ZR,
} InputEvent;

#define INPUT_MAX_EVENTS 16

typedef struct {
    InputEvent events[INPUT_MAX_EVENTS];
    int count;
} InputState;

void inputInit(void);
void inputPoll(InputState *state);

/* Touch screen. While it draws a frame the UI says what can be tapped, and
   inputPoll turns a tap into the button it stands for, so the screens
   handle touch and a controller the same way. */
void inputTouchReset(void);
void inputTouchButton(int x, int y, int w, int h, InputEvent ev);
/* a list row: a tap moves *cursor to value, then sends ev (INPUT_NONE to
   only select) */
void inputTouchRow(int x, int y, int w, int h, int *cursor, int value, InputEvent ev);
/* dragging a finger down the screen sends `back` every step_px, dragging
   up sends `forward` */
void inputTouchSwipe(InputEvent back, InputEvent forward, int step_px);

#endif
