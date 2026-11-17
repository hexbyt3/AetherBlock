#include "input.h"
#include <stdlib.h>

typedef struct {
    u64 button_mask;
    InputEvent event;
    u64 held_start_tick;
    bool repeat_started;
} ButtonMap;

static ButtonMap s_buttons[] = {
    { HidNpadButton_Up    | HidNpadButton_StickLUp,    INPUT_UP,    0, false },
    { HidNpadButton_Down  | HidNpadButton_StickLDown,   INPUT_DOWN,  0, false },
    { HidNpadButton_Left  | HidNpadButton_StickLLeft,   INPUT_LEFT,  0, false },
    { HidNpadButton_Right | HidNpadButton_StickLRight,  INPUT_RIGHT, 0, false },
    { HidNpadButton_A,                                   INPUT_A,     0, false },
    { HidNpadButton_B,                                   INPUT_B,     0, false },
    { HidNpadButton_X,                                   INPUT_X,     0, false },
    { HidNpadButton_Y,                                   INPUT_Y,     0, false },
    { HidNpadButton_Plus,                                INPUT_PLUS,  0, false },
    { HidNpadButton_Minus,                               INPUT_MINUS, 0, false },
    { HidNpadButton_L,                                   INPUT_L,     0, false },
    { HidNpadButton_R,                                   INPUT_R,     0, false },
    { HidNpadButton_ZL,                                  INPUT_ZL,    0, false },
    { HidNpadButton_ZR,                                  INPUT_ZR,    0, false },
};

#define BUTTON_COUNT (sizeof(s_buttons) / sizeof(s_buttons[0]))

static PadState s_pad;

typedef struct {
    int x, y, w, h;
    InputEvent ev;
    int *cursor;
    int value;
} TouchZone;

#define TOUCH_MAX_ZONES 64
/* a finger that travels further than this was not a tap */
#define TAP_SLOP_PX 24

static TouchZone s_zones[TOUCH_MAX_ZONES];
static int s_zone_count;

static InputEvent s_swipe_back, s_swipe_forward;
static int s_swipe_step;

static bool s_touching, s_dragged;
static int s_start_x, s_start_y, s_anchor_y;

void inputInit(void) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&s_pad);
    hidInitializeTouchScreen();
}

void inputTouchReset(void) {
    s_zone_count = 0;
    s_swipe_step = 0;
}

void inputTouchRow(int x, int y, int w, int h, int *cursor, int value, InputEvent ev) {
    if (s_zone_count >= TOUCH_MAX_ZONES || w <= 0 || h <= 0)
        return;
    s_zones[s_zone_count++] = (TouchZone){ x, y, w, h, ev, cursor, value };
}

void inputTouchButton(int x, int y, int w, int h, InputEvent ev) {
    inputTouchRow(x, y, w, h, NULL, 0, ev);
}

void inputTouchSwipe(InputEvent back, InputEvent forward, int step_px) {
    s_swipe_back = back;
    s_swipe_forward = forward;
    s_swipe_step = step_px;
}

static void pushEvent(InputState *state, InputEvent ev) {
    if (state->count < INPUT_MAX_EVENTS)
        state->events[state->count++] = ev;
}

static void tap(InputState *state, int x, int y) {
    /* newest first: what was drawn last is on top */
    for (int i = s_zone_count - 1; i >= 0; i--) {
        TouchZone *z = &s_zones[i];
        if (x < z->x || x >= z->x + z->w || y < z->y || y >= z->y + z->h)
            continue;
        if (z->cursor)
            *z->cursor = z->value;
        if (z->ev != INPUT_NONE)
            pushEvent(state, z->ev);
        return;
    }
}

static void pollTouch(InputState *state) {
    HidTouchScreenState ts = {0};
    bool touching = hidGetTouchScreenStates(&ts, 1) > 0 && ts.count > 0;

    if (touching) {
        int x = (int)ts.touches[0].x;
        int y = (int)ts.touches[0].y;

        if (!s_touching) {
            s_start_x = x;
            s_start_y = s_anchor_y = y;
            s_dragged = false;
        }
        if (abs(x - s_start_x) > TAP_SLOP_PX || abs(y - s_start_y) > TAP_SLOP_PX)
            s_dragged = true;

        if (s_swipe_step > 0) {
            while (y - s_anchor_y >= s_swipe_step) {
                pushEvent(state, s_swipe_back);
                s_anchor_y += s_swipe_step;
            }
            while (s_anchor_y - y >= s_swipe_step) {
                pushEvent(state, s_swipe_forward);
                s_anchor_y -= s_swipe_step;
            }
        }
    } else if (s_touching && !s_dragged) {
        /* lifted without moving: that was a tap */
        tap(state, s_start_x, s_start_y);
    }

    s_touching = touching;
}

void inputPoll(InputState *state) {
    state->count = 0;
    padUpdate(&s_pad);
    pollTouch(state);

    u64 down = padGetButtonsDown(&s_pad);
    u64 held = padGetButtons(&s_pad);
    u64 now  = armGetSystemTick();

    u64 ticks_first = (u64)KEY_REPEAT_FIRST_MS * 19200;
    u64 ticks_repeat = (u64)KEY_REPEAT_MS * 19200;

    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        ButtonMap *b = &s_buttons[i];

        if (down & b->button_mask) {
            pushEvent(state, b->event);
            b->held_start_tick = now;
            b->repeat_started = false;
        } else if (held & b->button_mask) {
            u64 elapsed = now - b->held_start_tick;
            if (!b->repeat_started) {
                if (elapsed >= ticks_first) {
                    pushEvent(state, b->event);
                    b->repeat_started = true;
                    b->held_start_tick = now;
                }
            } else {
                if (elapsed >= ticks_repeat) {
                    pushEvent(state, b->event);
                    b->held_start_tick = now;
                }
            }
        } else {
            b->held_start_tick = 0;
            b->repeat_started = false;
        }
    }
}
