#ifndef GUI_INPUT_H
#define GUI_INPUT_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef enum GuiInputAction {
    GUI_INPUT_NONE,
    GUI_INPUT_QUIT,
    GUI_INPUT_PAUSE,
    GUI_INPUT_SPEED_UP,
    GUI_INPUT_SLOW_DOWN,
    GUI_INPUT_SELECT_NEXT,
    GUI_INPUT_SELECT_PREV,
    GUI_INPUT_DESELECT,
    GUI_INPUT_RESET,
    GUI_INPUT_TOGGLE_GRID,
    GUI_INPUT_TOGGLE_INFO,
    GUI_INPUT_ZOOM_IN,
    GUI_INPUT_ZOOM_OUT,
    GUI_INPUT_PAN_UP,
    GUI_INPUT_PAN_DOWN,
    GUI_INPUT_PAN_LEFT,
    GUI_INPUT_PAN_RIGHT,
    GUI_INPUT_CLICK,
    GUI_INPUT_DRAG,
    GUI_INPUT_SCROLL
} GuiInputAction;

typedef struct GuiInputState {
    // Mouse state
    int mouse_x, mouse_y;
    int mouse_dx, mouse_dy;         // Mouse motion delta
    bool mouse_left_down;
    bool mouse_right_down;
    bool mouse_middle_down;
    int scroll_delta;               // Mouse wheel scroll
    
    // Keyboard modifiers
    bool shift_held;
    bool ctrl_held;
    bool alt_held;
    
    // Click detection
    bool clicked;
    int click_x, click_y;
    
    // Current action
    GuiInputAction action;
} GuiInputState;

// Initialize input system
void gui_input_init(void);

// Process SDL events and return input state
// Returns false if should quit
bool gui_input_process(GuiInputState* state);

// Check if specific keys are pressed
bool gui_input_key_pressed(SDL_Keycode key);

#endif // GUI_INPUT_H
