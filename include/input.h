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
} InputEvent;

#define INPUT_MAX_EVENTS 16

typedef struct {
    InputEvent events[INPUT_MAX_EVENTS];
    int count;
} InputState;

void inputInit(void);
void inputPoll(InputState *state);

#endif
