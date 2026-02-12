#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

typedef enum InputAction {
    INPUT_NONE,
    INPUT_QUIT,
    INPUT_PAUSE,
    INPUT_SPEED_UP,
    INPUT_SLOW_DOWN,
    INPUT_SCROLL_UP,
    INPUT_SCROLL_DOWN,
    INPUT_SCROLL_LEFT,
    INPUT_SCROLL_RIGHT,
    INPUT_SELECT,
    INPUT_DESELECT,
    INPUT_RESET
} InputAction;

// Terminal mode control
void input_init(void);      // Set terminal to raw mode
void input_cleanup(void);   // Restore terminal

// Input polling
InputAction input_poll(void);     // Non-blocking input check
int input_poll_char(void);        // Get raw character (-1 if none)
bool input_is_initialized(void);  // Check if input system is ready

#endif // INPUT_H
