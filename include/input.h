#ifndef INPUT_H
#define INPUT_H

/**
 * @file input.h
 * @brief Gamepad input abstraction with key-repeat support.
 *
 * Translates raw Nintendo Switch HID input into a queue of logical
 * InputEvent values, handling initial and sustained key-repeat timing.
 */

#include <switch.h>
#include <stdbool.h>
#include "config.h"

/** Logical input events produced by the input subsystem. */
typedef enum {
    INPUT_NONE = 0, /**< No event */
    INPUT_UP,       /**< D-pad / stick up */
    INPUT_DOWN,     /**< D-pad / stick down */
    INPUT_LEFT,     /**< D-pad / stick left */
    INPUT_RIGHT,    /**< D-pad / stick right */
    INPUT_A,        /**< A button (confirm) */
    INPUT_B,        /**< B button (back) */
    INPUT_X,        /**< X button */
    INPUT_Y,        /**< Y button */
    INPUT_PLUS,     /**< Plus (+) button */
    INPUT_MINUS,    /**< Minus (-) button */
    INPUT_L,        /**< L shoulder button */
    INPUT_R,        /**< R shoulder button */
} InputEvent;

/** Maximum number of events that can be queued in a single poll cycle. */
#define INPUT_MAX_EVENTS 16

/** Snapshot of input events collected during one poll cycle. */
typedef struct {
    InputEvent events[INPUT_MAX_EVENTS]; /**< Queued events */
    int count;                           /**< Number of valid events */
} InputState;

/** Initialise the input subsystem (call once at startup). */
void inputInit(void);

/** Poll for new input events and populate @p state. */
void inputPoll(InputState *state);

#endif
