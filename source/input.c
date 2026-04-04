#include "input.h"

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

void inputInit(void) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&s_pad);
}

static void pushEvent(InputState *state, InputEvent ev) {
    if (state->count < INPUT_MAX_EVENTS)
        state->events[state->count++] = ev;
}

void inputPoll(InputState *state) {
    state->count = 0;
    padUpdate(&s_pad);

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
