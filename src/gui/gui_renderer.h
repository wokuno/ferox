#ifndef GUI_RENDERER_H
#define GUI_RENDERER_H

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "../shared/protocol.h"

// Default window dimensions
#define GUI_DEFAULT_WIDTH 1280
#define GUI_DEFAULT_HEIGHT 720

// Rendering settings
#define COLONY_SEGMENTS 64      // Number of segments for colony border
#define GRID_CELL_SIZE 20       // Size of grid cells in pixels

// Colony info panel dimensions
#define INFO_PANEL_WIDTH 250
#define INFO_PANEL_HEIGHT 200
#define INFO_PANEL_MARGIN 10

typedef struct GuiRenderer {
    SDL_Window* window;
    SDL_Renderer* renderer;
    
    // Window dimensions
    int window_width;
    int window_height;
    
    // Viewport (world coordinates)
    float view_x, view_y;       // Center of view in world coords
    float zoom;                 // Zoom level (pixels per world unit)
    
    // Display options
    bool show_grid;
    bool show_info_panel;
    bool antialiasing;
    
    // Selection
    uint32_t selected_colony;
    
    // Animation time
    float time;
} GuiRenderer;

// Create and destroy
GuiRenderer* gui_renderer_create(const char* title);
void gui_renderer_destroy(GuiRenderer* renderer);

// Frame management
void gui_renderer_clear(GuiRenderer* renderer);
void gui_renderer_present(GuiRenderer* renderer);

// World rendering
void gui_renderer_draw_world(GuiRenderer* renderer, const ProtoWorld* world);
void gui_renderer_draw_colony(GuiRenderer* renderer, const ProtoColony* colony, bool selected);
void gui_renderer_draw_grid(GuiRenderer* renderer, int world_width, int world_height);
void gui_renderer_draw_petri_dish(GuiRenderer* renderer, int world_width, int world_height);

// UI rendering
void gui_renderer_draw_colony_info(GuiRenderer* renderer, const ProtoColony* colony);
void gui_renderer_draw_status_bar(GuiRenderer* renderer, uint32_t tick, int colony_count, 
                                   bool paused, float speed);
void gui_renderer_draw_controls_help(GuiRenderer* renderer);

// Coordinate conversion
void gui_renderer_world_to_screen(GuiRenderer* renderer, float wx, float wy, int* sx, int* sy);
void gui_renderer_screen_to_world(GuiRenderer* renderer, int sx, int sy, float* wx, float* wy);

// Viewport control
void gui_renderer_set_zoom(GuiRenderer* renderer, float zoom);
void gui_renderer_pan(GuiRenderer* renderer, float dx, float dy);
void gui_renderer_center_on(GuiRenderer* renderer, float x, float y);
void gui_renderer_zoom_at(GuiRenderer* renderer, int screen_x, int screen_y, float factor);

// Settings
void gui_renderer_toggle_grid(GuiRenderer* renderer);
void gui_renderer_toggle_info_panel(GuiRenderer* renderer);

// Update time for animations
void gui_renderer_update_time(GuiRenderer* renderer, float dt);

// Text rendering (simple bitmap font)
void gui_renderer_draw_text(GuiRenderer* renderer, int x, int y, const char* text,
                            uint8_t r, uint8_t g, uint8_t b);
void gui_renderer_draw_text_scaled(GuiRenderer* renderer, int x, int y, const char* text,
                                   uint8_t r, uint8_t g, uint8_t b, int scale);

#endif // GUI_RENDERER_H
