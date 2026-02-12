#include "gui_input.h"
#include <string.h>

static const Uint8* keyboard_state = NULL;

void gui_input_init(void) {
    keyboard_state = SDL_GetKeyboardState(NULL);
}

bool gui_input_process(GuiInputState* state) {
    if (!state) return false;
    
    memset(state, 0, sizeof(GuiInputState));
    state->action = GUI_INPUT_NONE;
    
    // Get keyboard modifier state
    SDL_Keymod mod = SDL_GetModState();
    state->shift_held = (mod & KMOD_SHIFT) != 0;
    state->ctrl_held = (mod & KMOD_CTRL) != 0;
    state->alt_held = (mod & KMOD_ALT) != 0;
    
    // Get mouse state
    Uint32 mouse_buttons = SDL_GetMouseState(&state->mouse_x, &state->mouse_y);
    state->mouse_left_down = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    state->mouse_right_down = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    state->mouse_middle_down = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                return false;
                
            case SDL_KEYDOWN:
                if (event.key.repeat) break;  // Ignore key repeats
                
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                    case SDLK_q:
                        return false;
                        
                    case SDLK_SPACE:
                        state->action = GUI_INPUT_PAUSE;
                        break;
                        
                    case SDLK_EQUALS:
                    case SDLK_PLUS:
                    case SDLK_KP_PLUS:
                        state->action = GUI_INPUT_SPEED_UP;
                        break;
                        
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        state->action = GUI_INPUT_SLOW_DOWN;
                        break;
                        
                    case SDLK_TAB:
                        if (state->shift_held) {
                            state->action = GUI_INPUT_SELECT_PREV;
                        } else {
                            state->action = GUI_INPUT_SELECT_NEXT;
                        }
                        break;
                        
                    case SDLK_n:
                        state->action = GUI_INPUT_SELECT_NEXT;
                        break;
                        
                    case SDLK_p:
                        state->action = GUI_INPUT_SELECT_PREV;
                        break;
                        
                    case SDLK_d:
                        state->action = GUI_INPUT_DESELECT;
                        break;
                        
                    case SDLK_r:
                        state->action = GUI_INPUT_RESET;
                        break;
                        
                    case SDLK_g:
                        state->action = GUI_INPUT_TOGGLE_GRID;
                        break;
                        
                    case SDLK_i:
                        state->action = GUI_INPUT_TOGGLE_INFO;
                        break;
                        
                    case SDLK_z:
                        if (state->shift_held) {
                            state->action = GUI_INPUT_ZOOM_OUT;
                        } else {
                            state->action = GUI_INPUT_ZOOM_IN;
                        }
                        break;
                        
                    case SDLK_UP:
                    case SDLK_w:
                        state->action = GUI_INPUT_PAN_UP;
                        break;
                        
                    case SDLK_DOWN:
                    case SDLK_s:
                        state->action = GUI_INPUT_PAN_DOWN;
                        break;
                        
                    case SDLK_LEFT:
                    case SDLK_a:
                        state->action = GUI_INPUT_PAN_LEFT;
                        break;
                        
                    case SDLK_RIGHT:
                        state->action = GUI_INPUT_PAN_RIGHT;
                        break;
                }
                break;
                
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    state->clicked = true;
                    state->click_x = event.button.x;
                    state->click_y = event.button.y;
                    state->action = GUI_INPUT_CLICK;
                }
                break;
                
            case SDL_MOUSEMOTION:
                state->mouse_dx = event.motion.xrel;
                state->mouse_dy = event.motion.yrel;
                if (state->mouse_right_down || state->mouse_middle_down) {
                    state->action = GUI_INPUT_DRAG;
                }
                break;
                
            case SDL_MOUSEWHEEL:
                state->scroll_delta = event.wheel.y;
                if (state->scroll_delta != 0) {
                    state->action = GUI_INPUT_SCROLL;
                }
                break;
                
            case SDL_WINDOWEVENT:
                // Handle window resize, etc.
                break;
        }
    }
    
    return true;
}

bool gui_input_key_pressed(SDL_Keycode key) {
    if (!keyboard_state) return false;
    SDL_Scancode scancode = SDL_GetScancodeFromKey(key);
    return keyboard_state[scancode] != 0;
}
