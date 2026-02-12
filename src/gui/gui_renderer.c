#include "gui_renderer.h"
#include "../shared/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Simple 5x7 bitmap font (covers ASCII 32-126)
// Each character is 5 pixels wide, 7 pixels tall
static const uint8_t FONT_5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x00,0x00,0x5F,0x00,0x00}, // 33 '!'
    {0x00,0x07,0x00,0x07,0x00}, // 34 '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 '$'
    {0x23,0x13,0x08,0x64,0x62}, // 37 '%'
    {0x36,0x49,0x55,0x22,0x50}, // 38 '&'
    {0x00,0x05,0x03,0x00,0x00}, // 39 '''
    {0x00,0x1C,0x22,0x41,0x00}, // 40 '('
    {0x00,0x41,0x22,0x1C,0x00}, // 41 ')'
    {0x08,0x2A,0x1C,0x2A,0x08}, // 42 '*'
    {0x08,0x08,0x3E,0x08,0x08}, // 43 '+'
    {0x00,0x50,0x30,0x00,0x00}, // 44 ','
    {0x08,0x08,0x08,0x08,0x08}, // 45 '-'
    {0x00,0x60,0x60,0x00,0x00}, // 46 '.'
    {0x20,0x10,0x08,0x04,0x02}, // 47 '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 '0'
    {0x00,0x42,0x7F,0x40,0x00}, // 49 '1'
    {0x42,0x61,0x51,0x49,0x46}, // 50 '2'
    {0x21,0x41,0x45,0x4B,0x31}, // 51 '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 52 '4'
    {0x27,0x45,0x45,0x45,0x39}, // 53 '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 '6'
    {0x01,0x71,0x09,0x05,0x03}, // 55 '7'
    {0x36,0x49,0x49,0x49,0x36}, // 56 '8'
    {0x06,0x49,0x49,0x29,0x1E}, // 57 '9'
    {0x00,0x36,0x36,0x00,0x00}, // 58 ':'
    {0x00,0x56,0x36,0x00,0x00}, // 59 ';'
    {0x00,0x08,0x14,0x22,0x41}, // 60 '<'
    {0x14,0x14,0x14,0x14,0x14}, // 61 '='
    {0x41,0x22,0x14,0x08,0x00}, // 62 '>'
    {0x02,0x01,0x51,0x09,0x06}, // 63 '?'
    {0x32,0x49,0x79,0x41,0x3E}, // 64 '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 66 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 67 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 69 'E'
    {0x7F,0x09,0x09,0x01,0x01}, // 70 'F'
    {0x3E,0x41,0x41,0x51,0x32}, // 71 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 73 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 74 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 75 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 76 'L'
    {0x7F,0x02,0x04,0x02,0x7F}, // 77 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 80 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 82 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 83 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 84 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 'V'
    {0x7F,0x20,0x18,0x20,0x7F}, // 87 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 88 'X'
    {0x03,0x04,0x78,0x04,0x03}, // 89 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 90 'Z'
    {0x00,0x00,0x7F,0x41,0x41}, // 91 '['
    {0x02,0x04,0x08,0x10,0x20}, // 92 '\'
    {0x41,0x41,0x7F,0x00,0x00}, // 93 ']'
    {0x04,0x02,0x01,0x02,0x04}, // 94 '^'
    {0x40,0x40,0x40,0x40,0x40}, // 95 '_'
    {0x00,0x01,0x02,0x04,0x00}, // 96 '`'
    {0x20,0x54,0x54,0x54,0x78}, // 97 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 98 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 99 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 100 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 101 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 102 'f'
    {0x08,0x14,0x54,0x54,0x3C}, // 103 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 104 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 105 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 106 'j'
    {0x00,0x7F,0x10,0x28,0x44}, // 107 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 108 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 109 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 110 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 111 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 112 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 113 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 114 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 115 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 116 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 117 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 118 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 119 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 120 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 121 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 122 'z'
    {0x00,0x08,0x36,0x41,0x00}, // 123 '{'
    {0x00,0x00,0x7F,0x00,0x00}, // 124 '|'
    {0x00,0x41,0x36,0x08,0x00}, // 125 '}'
    {0x08,0x08,0x2A,0x1C,0x08}, // 126 '~'
};

// Anti-aliased line drawing using Wu's algorithm
static void draw_aa_line(SDL_Renderer* r, float x1, float y1, float x2, float y2,
                         uint8_t r_col, uint8_t g_col, uint8_t b_col);

GuiRenderer* gui_renderer_create(const char* title) {
    GuiRenderer* renderer = (GuiRenderer*)calloc(1, sizeof(GuiRenderer));
    if (!renderer) return NULL;
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        free(renderer);
        return NULL;
    }
    
    // Create window
    renderer->window = SDL_CreateWindow(
        title ? title : "Ferox - Bacterial Colony Simulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        GUI_DEFAULT_WIDTH, GUI_DEFAULT_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!renderer->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        free(renderer);
        return NULL;
    }
    
    // Create renderer with hardware acceleration
    renderer->renderer = SDL_CreateRenderer(
        renderer->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    
    if (!renderer->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(renderer->window);
        SDL_Quit();
        free(renderer);
        return NULL;
    }
    
    // Enable alpha blending for anti-aliasing
    SDL_SetRenderDrawBlendMode(renderer->renderer, SDL_BLENDMODE_BLEND);
    
    // Initialize state
    renderer->window_width = GUI_DEFAULT_WIDTH;
    renderer->window_height = GUI_DEFAULT_HEIGHT;
    renderer->view_x = 50.0f;  // Center of default 100x100 world
    renderer->view_y = 50.0f;
    renderer->zoom = 6.0f;     // 6 pixels per world unit
    renderer->show_grid = false;
    renderer->show_info_panel = true;
    renderer->antialiasing = true;
    renderer->selected_colony = 0;
    renderer->time = 0.0f;
    
    return renderer;
}

void gui_renderer_destroy(GuiRenderer* renderer) {
    if (!renderer) return;
    
    if (renderer->renderer) {
        SDL_DestroyRenderer(renderer->renderer);
    }
    if (renderer->window) {
        SDL_DestroyWindow(renderer->window);
    }
    SDL_Quit();
    free(renderer);
}

void gui_renderer_clear(GuiRenderer* renderer) {
    if (!renderer || !renderer->renderer) return;
    
    // Update window size if changed
    SDL_GetWindowSize(renderer->window, &renderer->window_width, &renderer->window_height);
    
    // Dark background
    SDL_SetRenderDrawColor(renderer->renderer, 20, 20, 30, 255);
    SDL_RenderClear(renderer->renderer);
}

void gui_renderer_present(GuiRenderer* renderer) {
    if (!renderer || !renderer->renderer) return;
    SDL_RenderPresent(renderer->renderer);
}

void gui_renderer_world_to_screen(GuiRenderer* renderer, float wx, float wy, int* sx, int* sy) {
    float cx = renderer->window_width / 2.0f;
    float cy = renderer->window_height / 2.0f;
    
    *sx = (int)(cx + (wx - renderer->view_x) * renderer->zoom);
    *sy = (int)(cy + (wy - renderer->view_y) * renderer->zoom);
}

void gui_renderer_screen_to_world(GuiRenderer* renderer, int sx, int sy, float* wx, float* wy) {
    float cx = renderer->window_width / 2.0f;
    float cy = renderer->window_height / 2.0f;
    
    *wx = renderer->view_x + (sx - cx) / renderer->zoom;
    *wy = renderer->view_y + (sy - cy) / renderer->zoom;
}

void gui_renderer_set_zoom(GuiRenderer* renderer, float zoom) {
    if (!renderer) return;
    renderer->zoom = zoom;
    if (renderer->zoom < 1.0f) renderer->zoom = 1.0f;
    if (renderer->zoom > 50.0f) renderer->zoom = 50.0f;
}

void gui_renderer_pan(GuiRenderer* renderer, float dx, float dy) {
    if (!renderer) return;
    renderer->view_x += dx;
    renderer->view_y += dy;
}

void gui_renderer_center_on(GuiRenderer* renderer, float x, float y) {
    if (!renderer) return;
    renderer->view_x = x;
    renderer->view_y = y;
}

void gui_renderer_zoom_at(GuiRenderer* renderer, int screen_x, int screen_y, float factor) {
    if (!renderer) return;
    
    // Get world position under cursor before zoom
    float world_x, world_y;
    gui_renderer_screen_to_world(renderer, screen_x, screen_y, &world_x, &world_y);
    
    // Apply zoom
    float new_zoom = renderer->zoom * factor;
    if (new_zoom < 1.0f) new_zoom = 1.0f;
    if (new_zoom > 50.0f) new_zoom = 50.0f;
    renderer->zoom = new_zoom;
    
    // Adjust view to keep cursor position stable
    float new_world_x, new_world_y;
    gui_renderer_screen_to_world(renderer, screen_x, screen_y, &new_world_x, &new_world_y);
    
    renderer->view_x += world_x - new_world_x;
    renderer->view_y += world_y - new_world_y;
}

void gui_renderer_toggle_grid(GuiRenderer* renderer) {
    if (!renderer) return;
    renderer->show_grid = !renderer->show_grid;
}

void gui_renderer_toggle_info_panel(GuiRenderer* renderer) {
    if (!renderer) return;
    renderer->show_info_panel = !renderer->show_info_panel;
}

void gui_renderer_update_time(GuiRenderer* renderer, float dt) {
    if (!renderer) return;
    renderer->time += dt;
}

// Draw anti-aliased line using Wu's algorithm
static void draw_aa_line(SDL_Renderer* r, float x1, float y1, float x2, float y2,
                         uint8_t r_col, uint8_t g_col, uint8_t b_col) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    
    bool steep = fabsf(dy) > fabsf(dx);
    
    if (steep) {
        float tmp = x1; x1 = y1; y1 = tmp;
        tmp = x2; x2 = y2; y2 = tmp;
    }
    
    if (x1 > x2) {
        float tmp = x1; x1 = x2; x2 = tmp;
        tmp = y1; y1 = y2; y2 = tmp;
    }
    
    dx = x2 - x1;
    dy = y2 - y1;
    float gradient = (dx == 0) ? 1.0f : dy / dx;
    
    // First endpoint
    float xend = roundf(x1);
    float yend = y1 + gradient * (xend - x1);
    float xpxl1 = xend;
    (void)floorf(yend);  // Endpoint drawing omitted for simplicity
    
    float intery = yend + gradient;
    
    // Second endpoint
    xend = roundf(x2);
    float xpxl2 = xend;
    
    // Main loop
    for (float x = xpxl1 + 1; x < xpxl2; x++) {
        float fpart = intery - floorf(intery);
        
        int px1 = steep ? (int)floorf(intery) : (int)x;
        int py1 = steep ? (int)x : (int)floorf(intery);
        int px2 = steep ? (int)floorf(intery) + 1 : (int)x;
        int py2 = steep ? (int)x : (int)floorf(intery) + 1;
        
        SDL_SetRenderDrawColor(r, r_col, g_col, b_col, (uint8_t)(255 * (1 - fpart)));
        SDL_RenderDrawPoint(r, px1, py1);
        SDL_SetRenderDrawColor(r, r_col, g_col, b_col, (uint8_t)(255 * fpart));
        SDL_RenderDrawPoint(r, px2, py2);
        
        intery += gradient;
    }
}

// Draw a single colony with organic shape
void gui_renderer_draw_colony(GuiRenderer* renderer, const ProtoColony* colony, bool selected) {
    if (!renderer || !colony || !colony->alive) return;
    
    SDL_Renderer* r = renderer->renderer;
    
    // Calculate screen position
    int cx, cy;
    gui_renderer_world_to_screen(renderer, colony->x, colony->y, &cx, &cy);
    
    float screen_radius = colony->radius * renderer->zoom;
    
    // Skip if too small or off screen
    if (screen_radius < 1.0f || !isfinite(screen_radius)) return;
    if (cx + screen_radius < 0 || cx - screen_radius > renderer->window_width) return;
    if (cy + screen_radius < 0 || cy - screen_radius > renderer->window_height) return;
    
    // Generate points for organic shape
    float points_x[COLONY_SEGMENTS];
    float points_y[COLONY_SEGMENTS];
    
    for (int i = 0; i < COLONY_SEGMENTS; i++) {
        float angle = (float)i / COLONY_SEGMENTS * 2.0f * M_PI;
        float shape_mult = colony_shape_at_angle_evolved(colony->shape_seed, angle, 
                                                   colony->wobble_phase + renderer->time * 0.5f,
                                                   colony->shape_evolution);
        if (!isfinite(shape_mult) || shape_mult < 0.1f) shape_mult = 1.0f;
        float r_at_angle = screen_radius * shape_mult;
        
        points_x[i] = cx + cosf(angle) * r_at_angle;
        points_y[i] = cy + sinf(angle) * r_at_angle;
    }
    
    // Draw filled interior with gradient effect
    // Use triangle fan from center
    uint8_t col_r = colony->color_r;
    uint8_t col_g = colony->color_g;
    uint8_t col_b = colony->color_b;
    
    // Draw filled triangles
    for (int i = 0; i < COLONY_SEGMENTS; i++) {
        int next = (i + 1) % COLONY_SEGMENTS;
        
        // Draw triangle as lines (SDL2 doesn't have fill triangle)
        // Fill with horizontal lines
        float x1 = points_x[i];
        float y1 = points_y[i];
        float x2 = points_x[next];
        float y2 = points_y[next];
        
        // Simple scanline fill for each triangle slice
        int min_y = (int)fminf(fminf(cy, y1), y2);
        int max_y = (int)fmaxf(fmaxf(cy, y1), y2);
        
        for (int sy = min_y; sy <= max_y; sy++) {
            // Find intersection points
            float intersections[3];
            int num_intersect = 0;
            
            // Edge from center to p1
            float dy1 = y1 - cy;
            if (fabsf(dy1) > 0.01f && ((cy <= sy && y1 >= sy) || (y1 <= sy && cy >= sy))) {
                float t = (sy - cy) / dy1;
                float x_int = cx + t * (x1 - cx);
                if (isfinite(x_int)) intersections[num_intersect++] = x_int;
            }
            // Edge from p1 to p2
            float dy2 = y2 - y1;
            if (fabsf(dy2) > 0.01f && ((y1 <= sy && y2 >= sy) || (y2 <= sy && y1 >= sy))) {
                float t = (sy - y1) / dy2;
                float x_int = x1 + t * (x2 - x1);
                if (isfinite(x_int)) intersections[num_intersect++] = x_int;
            }
            // Edge from p2 to center
            float dy3 = cy - y2;
            if (fabsf(dy3) > 0.01f && ((y2 <= sy && cy >= sy) || (cy <= sy && y2 >= sy))) {
                float t = (sy - y2) / dy3;
                float x_int = x2 + t * (cx - x2);
                if (isfinite(x_int)) intersections[num_intersect++] = x_int;
            }
            
            if (num_intersect >= 2) {
                float left = fminf(intersections[0], intersections[1]);
                float right = fmaxf(intersections[0], intersections[1]);
                
                // Skip invalid lines
                if (!isfinite(left) || !isfinite(right) || right - left > renderer->window_width) continue;
                
                // Calculate gradient based on distance from center
                float dist = sqrtf((float)((sy - cy) * (sy - cy)));
                float t = dist / (screen_radius + 1);
                if (t > 1.0f) t = 1.0f;
                
                // Lighter in center, darker at edges
                uint8_t grad_r = (uint8_t)(col_r * (1.0f - t * 0.3f));
                uint8_t grad_g = (uint8_t)(col_g * (1.0f - t * 0.3f));
                uint8_t grad_b = (uint8_t)(col_b * (1.0f - t * 0.3f));
                
                SDL_SetRenderDrawColor(r, grad_r, grad_g, grad_b, 200);
                SDL_RenderDrawLine(r, (int)left, sy, (int)right, sy);
            }
        }
    }
    
    // Draw anti-aliased border
    if (renderer->antialiasing) {
        uint8_t border_r = (uint8_t)(col_r * 0.7f);
        uint8_t border_g = (uint8_t)(col_g * 0.7f);
        uint8_t border_b = (uint8_t)(col_b * 0.7f);
        
        for (int i = 0; i < COLONY_SEGMENTS; i++) {
            int next = (i + 1) % COLONY_SEGMENTS;
            draw_aa_line(r, points_x[i], points_y[i], points_x[next], points_y[next],
                        border_r, border_g, border_b);
        }
    } else {
        SDL_SetRenderDrawColor(r, col_r * 0.7f, col_g * 0.7f, col_b * 0.7f, 255);
        for (int i = 0; i < COLONY_SEGMENTS; i++) {
            int next = (i + 1) % COLONY_SEGMENTS;
            SDL_RenderDrawLine(r, (int)points_x[i], (int)points_y[i],
                              (int)points_x[next], (int)points_y[next]);
        }
    }
    
    // Draw selection highlight
    if (selected) {
        float pulse = (sinf(renderer->time * 4.0f) + 1.0f) * 0.5f;
        uint8_t highlight_alpha = (uint8_t)(100 + pulse * 100);
        
        SDL_SetRenderDrawColor(r, 255, 255, 255, highlight_alpha);
        
        // Draw slightly larger border
        for (int i = 0; i < COLONY_SEGMENTS; i++) {
            float angle = (float)i / COLONY_SEGMENTS * 2.0f * M_PI;
            float shape_mult = colony_shape_at_angle_evolved(colony->shape_seed, angle,
                                                      colony->wobble_phase + renderer->time * 0.5f,
                                                      colony->shape_evolution);
            float r_at_angle = (screen_radius + 3) * shape_mult;
            
            int next = (i + 1) % COLONY_SEGMENTS;
            float next_angle = (float)next / COLONY_SEGMENTS * 2.0f * M_PI;
            float next_shape = colony_shape_at_angle_evolved(colony->shape_seed, next_angle,
                                                      colony->wobble_phase + renderer->time * 0.5f,
                                                      colony->shape_evolution);
            float next_r = (screen_radius + 3) * next_shape;
            
            SDL_RenderDrawLine(r,
                              cx + (int)(cosf(angle) * r_at_angle),
                              cy + (int)(sinf(angle) * r_at_angle),
                              cx + (int)(cosf(next_angle) * next_r),
                              cy + (int)(sinf(next_angle) * next_r));
        }
    }
}

void gui_renderer_draw_grid(GuiRenderer* renderer, int world_width, int world_height) {
    if (!renderer || !renderer->show_grid) return;
    
    SDL_Renderer* r = renderer->renderer;
    
    // Semi-transparent grid lines
    SDL_SetRenderDrawColor(r, 60, 60, 80, 100);
    
    // Calculate visible world bounds
    float world_left, world_top, world_right, world_bottom;
    gui_renderer_screen_to_world(renderer, 0, 0, &world_left, &world_top);
    gui_renderer_screen_to_world(renderer, renderer->window_width, renderer->window_height,
                                  &world_right, &world_bottom);
    
    // Grid spacing based on zoom
    float grid_spacing = 10.0f;
    if (renderer->zoom > 10) grid_spacing = 5.0f;
    if (renderer->zoom > 20) grid_spacing = 2.0f;
    if (renderer->zoom < 3) grid_spacing = 20.0f;
    
    // Vertical lines
    float start_x = floorf(world_left / grid_spacing) * grid_spacing;
    for (float wx = start_x; wx <= world_right && wx <= world_width; wx += grid_spacing) {
        if (wx < 0) continue;
        int sx1, sy1, sx2, sy2;
        gui_renderer_world_to_screen(renderer, wx, fmaxf(0, world_top), &sx1, &sy1);
        gui_renderer_world_to_screen(renderer, wx, fminf(world_height, world_bottom), &sx2, &sy2);
        SDL_RenderDrawLine(r, sx1, sy1, sx2, sy2);
    }
    
    // Horizontal lines
    float start_y = floorf(world_top / grid_spacing) * grid_spacing;
    for (float wy = start_y; wy <= world_bottom && wy <= world_height; wy += grid_spacing) {
        if (wy < 0) continue;
        int sx1, sy1, sx2, sy2;
        gui_renderer_world_to_screen(renderer, fmaxf(0, world_left), wy, &sx1, &sy1);
        gui_renderer_world_to_screen(renderer, fminf(world_width, world_right), wy, &sx2, &sy2);
        SDL_RenderDrawLine(r, sx1, sy1, sx2, sy2);
    }
}

void gui_renderer_draw_petri_dish(GuiRenderer* renderer, int world_width, int world_height) {
    if (!renderer) return;
    
    SDL_Renderer* r = renderer->renderer;
    
    // Draw filled background for the dish
    int x1, y1, x2, y2;
    gui_renderer_world_to_screen(renderer, 0, 0, &x1, &y1);
    gui_renderer_world_to_screen(renderer, world_width, world_height, &x2, &y2);
    
    // Darker background inside dish
    SDL_SetRenderDrawColor(r, 15, 15, 25, 255);
    SDL_Rect dish_rect = { x1, y1, x2 - x1, y2 - y1 };
    SDL_RenderFillRect(r, &dish_rect);
    
    // Draw border
    SDL_SetRenderDrawColor(r, 100, 100, 120, 255);
    SDL_RenderDrawRect(r, &dish_rect);
    
    // Draw corner decorations
    SDL_SetRenderDrawColor(r, 80, 80, 100, 255);
    int corner_size = 10;
    // Top-left
    SDL_RenderDrawLine(r, x1, y1 + corner_size, x1, y1);
    SDL_RenderDrawLine(r, x1, y1, x1 + corner_size, y1);
    // Top-right
    SDL_RenderDrawLine(r, x2 - corner_size, y1, x2, y1);
    SDL_RenderDrawLine(r, x2, y1, x2, y1 + corner_size);
    // Bottom-left
    SDL_RenderDrawLine(r, x1, y2 - corner_size, x1, y2);
    SDL_RenderDrawLine(r, x1, y2, x1 + corner_size, y2);
    // Bottom-right
    SDL_RenderDrawLine(r, x2 - corner_size, y2, x2, y2);
    SDL_RenderDrawLine(r, x2, y2, x2, y2 - corner_size);
}

void gui_renderer_draw_world(GuiRenderer* renderer, const ProtoWorld* world) {
    if (!renderer || !world) return;
    
    SDL_Renderer* r = renderer->renderer;
    
    // Draw petri dish background
    gui_renderer_draw_petri_dish(renderer, world->width, world->height);
    
    // Draw grid lines if enabled (low zoom)
    gui_renderer_draw_grid(renderer, world->width, world->height);
    
    // Get visible world bounds
    float world_left, world_top, world_right, world_bottom;
    gui_renderer_screen_to_world(renderer, 0, 0, &world_left, &world_top);
    gui_renderer_screen_to_world(renderer, renderer->window_width, renderer->window_height,
                                  &world_right, &world_bottom);
    
    // Clamp to world bounds
    int start_x = (int)world_left;
    int start_y = (int)world_top;
    int end_x = (int)world_right + 1;
    int end_y = (int)world_bottom + 1;
    if (start_x < 0) start_x = 0;
    if (start_y < 0) start_y = 0;
    if (end_x > (int)world->width) end_x = (int)world->width;
    if (end_y > (int)world->height) end_y = (int)world->height;
    
    float cell_size_pixels = renderer->zoom;
    
    // Use grid-based rendering if grid data is available
    if (world->has_grid && world->grid && world->grid_size > 0) {
        // Direct cell rendering from grid data
        for (int wy = start_y; wy < end_y; wy++) {
            for (int wx = start_x; wx < end_x; wx++) {
                int idx = wy * (int)world->width + wx;
                if (idx < 0 || idx >= (int)world->grid_size) continue;
                
                uint16_t colony_id = world->grid[idx];
                if (colony_id == 0) continue;  // Empty cell
                
                // Find colony data
                const ProtoColony* colony = NULL;
                for (uint32_t i = 0; i < world->colony_count; i++) {
                    if (world->colonies[i].id == colony_id && world->colonies[i].alive) {
                        colony = &world->colonies[i];
                        break;
                    }
                }
                if (!colony) continue;
                
                // Check if this is a border cell (adjacent to empty or different colony)
                bool is_border = false;
                const int dx[] = {0, 1, 0, -1};
                const int dy[] = {-1, 0, 1, 0};
                for (int d = 0; d < 4; d++) {
                    int nx = wx + dx[d];
                    int ny = wy + dy[d];
                    if (nx < 0 || nx >= (int)world->width || ny < 0 || ny >= (int)world->height) {
                        is_border = true;
                        break;
                    }
                    int nidx = ny * (int)world->width + nx;
                    if (world->grid[nidx] != colony_id) {
                        is_border = true;
                        break;
                    }
                }
                
                // Get screen position
                int sx, sy;
                gui_renderer_world_to_screen(renderer, (float)wx, (float)wy, &sx, &sy);
                
                // Calculate cell size
                int cell_w = (int)(cell_size_pixels + 0.5f);
                int cell_h = (int)(cell_size_pixels + 0.5f);
                if (cell_w < 1) cell_w = 1;
                if (cell_h < 1) cell_h = 1;
                
                // Color selection
                uint8_t col_r = colony->color_r;
                uint8_t col_g = colony->color_g;
                uint8_t col_b = colony->color_b;
                
                if (is_border) {
                    // Darker border color
                    col_r = (uint8_t)(col_r * 0.6f);
                    col_g = (uint8_t)(col_g * 0.6f);
                    col_b = (uint8_t)(col_b * 0.6f);
                }
                
                // Highlight selected colony
                if (colony->id == renderer->selected_colony) {
                    float pulse = (sinf(renderer->time * 4.0f) + 1.0f) * 0.5f;
                    col_r = (uint8_t)fminf(255, col_r + 40 * pulse);
                    col_g = (uint8_t)fminf(255, col_g + 40 * pulse);
                    col_b = (uint8_t)fminf(255, col_b + 40 * pulse);
                }
                
                SDL_SetRenderDrawColor(r, col_r, col_g, col_b, 255);
                SDL_Rect cell_rect = { sx, sy, cell_w, cell_h };
                SDL_RenderFillRect(r, &cell_rect);
            }
        }
    } else {
        // Fallback: simple colony center rendering (no grid data)
        for (uint32_t i = 0; i < world->colony_count; i++) {
            const ProtoColony* colony = &world->colonies[i];
            if (!colony->alive) continue;
            
            int cx, cy;
            gui_renderer_world_to_screen(renderer, colony->x, colony->y, &cx, &cy);
            int screen_radius = (int)(colony->radius * renderer->zoom);
            if (screen_radius < 2) screen_radius = 2;
            
            SDL_SetRenderDrawColor(r, colony->color_r, colony->color_g, colony->color_b, 255);
            SDL_Rect rect = { cx - screen_radius, cy - screen_radius, 
                             screen_radius * 2, screen_radius * 2 };
            SDL_RenderFillRect(r, &rect);
        }
    }
}

void gui_renderer_draw_colony_info(GuiRenderer* renderer, const ProtoColony* colony) {
    if (!renderer || !renderer->show_info_panel) return;
    
    SDL_Renderer* r = renderer->renderer;
    
    int panel_x = renderer->window_width - INFO_PANEL_WIDTH - INFO_PANEL_MARGIN;
    int panel_y = INFO_PANEL_MARGIN;
    
    // Panel background
    SDL_SetRenderDrawColor(r, 30, 30, 40, 220);
    SDL_Rect panel = { panel_x, panel_y, INFO_PANEL_WIDTH, INFO_PANEL_HEIGHT };
    SDL_RenderFillRect(r, &panel);
    
    // Panel border
    SDL_SetRenderDrawColor(r, 80, 80, 100, 255);
    SDL_RenderDrawRect(r, &panel);
    
    // Title bar
    SDL_SetRenderDrawColor(r, 50, 50, 70, 255);
    SDL_Rect title_bar = { panel_x + 1, panel_y + 1, INFO_PANEL_WIDTH - 2, 25 };
    SDL_RenderFillRect(r, &title_bar);
    
    // Title text
    gui_renderer_draw_text(renderer, panel_x + 10, panel_y + 8, "COLONY INFO", 180, 200, 255);
    
    if (!colony) {
        // No colony selected message
        gui_renderer_draw_text(renderer, panel_x + 20, panel_y + 50, "No colony selected", 150, 150, 170);
        gui_renderer_draw_text(renderer, panel_x + 20, panel_y + 70, "Press TAB to cycle", 120, 120, 140);
        return;
    }
    
    // Draw colony color swatch
    SDL_SetRenderDrawColor(r, colony->color_r, colony->color_g, colony->color_b, 255);
    SDL_Rect swatch = { panel_x + 10, panel_y + 35, 20, 20 };
    SDL_RenderFillRect(r, &swatch);
    SDL_SetRenderDrawColor(r, 200, 200, 220, 255);
    SDL_RenderDrawRect(r, &swatch);
    
    // Colony name (truncate if too long)
    char name_buf[24];
    snprintf(name_buf, sizeof(name_buf), "%.20s", colony->name);
    gui_renderer_draw_text(renderer, panel_x + 40, panel_y + 40, name_buf, 220, 220, 240);
    
    // Colony ID
    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "ID: %u", colony->id);
    gui_renderer_draw_text(renderer, panel_x + 40, panel_y + 55, id_buf, 150, 150, 170);
    
    // Position
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "Pos: %.0f, %.0f", colony->x, colony->y);
    gui_renderer_draw_text(renderer, panel_x + 10, panel_y + 75, pos_buf, 150, 160, 180);
    
    // Population label
    gui_renderer_draw_text(renderer, panel_x + 10, panel_y + 90, "Population:", 150, 160, 180);
    
    // Population bar
    int bar_y = panel_y + 100;
    int bar_width = INFO_PANEL_WIDTH - 40;
    float pop_ratio = (float)colony->population / (colony->max_population > 0 ? colony->max_population : 1);
    
    // Bar background
    SDL_SetRenderDrawColor(r, 50, 50, 60, 255);
    SDL_Rect bar_bg = { panel_x + 20, bar_y, bar_width, 15 };
    SDL_RenderFillRect(r, &bar_bg);
    
    // Bar fill
    SDL_SetRenderDrawColor(r, colony->color_r, colony->color_g, colony->color_b, 255);
    SDL_Rect bar_fill = { panel_x + 20, bar_y, (int)(bar_width * pop_ratio), 15 };
    SDL_RenderFillRect(r, &bar_fill);
    
    // Population numbers
    char pop_buf[32];
    snprintf(pop_buf, sizeof(pop_buf), "%d / %d", colony->population, colony->max_population);
    gui_renderer_draw_text(renderer, panel_x + 20, bar_y + 20, pop_buf, 180, 180, 200);
    
    // Growth indicator
    int growth_y = panel_y + 140;
    gui_renderer_draw_text(renderer, panel_x + 10, growth_y - 10, "Growth:", 150, 160, 180);
    
    SDL_SetRenderDrawColor(r, 100, 100, 120, 255);
    SDL_RenderDrawLine(r, panel_x + 20, growth_y + 7, panel_x + INFO_PANEL_WIDTH - 20, growth_y + 7);
    
    // Growth arrow
    if (colony->growth_rate > 0) {
        SDL_SetRenderDrawColor(r, 100, 200, 100, 255);  // Green for positive
    } else if (colony->growth_rate < 0) {
        SDL_SetRenderDrawColor(r, 200, 100, 100, 255);  // Red for negative
    } else {
        SDL_SetRenderDrawColor(r, 150, 150, 150, 255);  // Gray for neutral
    }
    
    int arrow_x = panel_x + INFO_PANEL_WIDTH / 2;
    int arrow_size = (int)(fabsf(colony->growth_rate) * 20);
    if (arrow_size > 30) arrow_size = 30;
    
    if (colony->growth_rate > 0) {
        SDL_RenderDrawLine(r, arrow_x, growth_y + 12, arrow_x, growth_y + 12 - arrow_size);
        SDL_RenderDrawLine(r, arrow_x - 5, growth_y + 7 - arrow_size, arrow_x, growth_y + 2 - arrow_size);
        SDL_RenderDrawLine(r, arrow_x + 5, growth_y + 7 - arrow_size, arrow_x, growth_y + 2 - arrow_size);
    } else if (colony->growth_rate < 0) {
        SDL_RenderDrawLine(r, arrow_x, growth_y + 2, arrow_x, growth_y + 2 + arrow_size);
        SDL_RenderDrawLine(r, arrow_x - 5, growth_y - 3 + arrow_size, arrow_x, growth_y + 2 + arrow_size);
        SDL_RenderDrawLine(r, arrow_x + 5, growth_y - 3 + arrow_size, arrow_x, growth_y + 2 + arrow_size);
    }
}

void gui_renderer_draw_status_bar(GuiRenderer* renderer, uint32_t tick, int colony_count,
                                   bool paused, float speed, float fps) {
    if (!renderer) return;
    
    SDL_Renderer* r = renderer->renderer;
    
    int bar_height = 30;
    int bar_y = renderer->window_height - bar_height;
    
    // Status bar background
    SDL_SetRenderDrawColor(r, 25, 25, 35, 240);
    SDL_Rect bar = { 0, bar_y, renderer->window_width, bar_height };
    SDL_RenderFillRect(r, &bar);
    
    // Top border
    SDL_SetRenderDrawColor(r, 60, 60, 80, 255);
    SDL_RenderDrawLine(r, 0, bar_y, renderer->window_width, bar_y);
    
    // Pause indicator
    if (paused) {
        SDL_SetRenderDrawColor(r, 200, 150, 50, 255);
        // Draw pause symbol
        SDL_Rect pause1 = { 15, bar_y + 8, 5, 14 };
        SDL_Rect pause2 = { 25, bar_y + 8, 5, 14 };
        SDL_RenderFillRect(r, &pause1);
        SDL_RenderFillRect(r, &pause2);
        gui_renderer_draw_text(renderer, 40, bar_y + 10, "PAUSED", 200, 150, 50);
    } else {
        SDL_SetRenderDrawColor(r, 100, 200, 100, 255);
        // Draw play symbol (triangle)
        for (int i = 0; i < 14; i++) {
            SDL_RenderDrawLine(r, 15, bar_y + 8 + i, 15 + (14 - i) / 2, bar_y + 8 + i);
        }
    }
    
    // Tick counter
    char tick_buf[32];
    snprintf(tick_buf, sizeof(tick_buf), "Tick: %u", tick);
    gui_renderer_draw_text(renderer, 100, bar_y + 10, tick_buf, 180, 180, 200);
    
    // Speed display
    char speed_buf[24];
    snprintf(speed_buf, sizeof(speed_buf), "Speed: %.1fx", speed);
    gui_renderer_draw_text(renderer, 220, bar_y + 10, speed_buf, 150, 180, 220);
    
    // FPS display
    char fps_buf[16];
    snprintf(fps_buf, sizeof(fps_buf), "FPS: %.0f", fps);
    gui_renderer_draw_text(renderer, 340, bar_y + 10, fps_buf, 150, 200, 150);
    
    // Colony count
    char colony_buf[24];
    snprintf(colony_buf, sizeof(colony_buf), "Colonies: %d", colony_count);
    gui_renderer_draw_text(renderer, renderer->window_width - 120, bar_y + 10, colony_buf, 180, 200, 180);
}

// Draw text using bitmap font
void gui_renderer_draw_text(GuiRenderer* renderer, int x, int y, const char* text,
                            uint8_t r_col, uint8_t g_col, uint8_t b_col) {
    gui_renderer_draw_text_scaled(renderer, x, y, text, r_col, g_col, b_col, 1);
}

void gui_renderer_draw_text_scaled(GuiRenderer* renderer, int x, int y, const char* text,
                                   uint8_t r_col, uint8_t g_col, uint8_t b_col, int scale) {
    if (!renderer || !text) return;
    SDL_Renderer* r = renderer->renderer;
    
    SDL_SetRenderDrawColor(r, r_col, g_col, b_col, 255);
    
    int cursor_x = x;
    for (const char* c = text; *c; c++) {
        int ch = (int)*c;
        if (ch < 32 || ch > 126) ch = 32; // Replace unprintable with space
        
        const uint8_t* glyph = FONT_5X7[ch - 32];
        
        // Draw each column of the glyph
        for (int col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row)) {
                    if (scale == 1) {
                        SDL_RenderDrawPoint(r, cursor_x + col, y + row);
                    } else {
                        SDL_Rect pixel = { cursor_x + col * scale, y + row * scale, scale, scale };
                        SDL_RenderFillRect(r, &pixel);
                    }
                }
            }
        }
        cursor_x += (5 + 1) * scale; // 5 pixel width + 1 pixel spacing
    }
}

void gui_renderer_draw_controls_help(GuiRenderer* renderer) {
    if (!renderer) return;
    
    SDL_Renderer* r = renderer->renderer;
    
    // Draw help box in top-left corner
    SDL_SetRenderDrawColor(r, 20, 25, 35, 220);
    SDL_Rect help_bg = { 10, 10, 200, 135 };
    SDL_RenderFillRect(r, &help_bg);
    
    SDL_SetRenderDrawColor(r, 80, 100, 140, 255);
    SDL_RenderDrawRect(r, &help_bg);
    
    // Title
    gui_renderer_draw_text(renderer, 15, 15, "CONTROLS", 180, 200, 255);
    
    // Navigation
    gui_renderer_draw_text(renderer, 15, 30, "WASD/Arrows: Pan", 150, 160, 180);
    gui_renderer_draw_text(renderer, 15, 42, "Z/X or Scroll: Zoom", 150, 160, 180);
    
    // Colony selection
    gui_renderer_draw_text(renderer, 15, 58, "Click: Select colony", 150, 160, 180);
    gui_renderer_draw_text(renderer, 15, 70, "Tab/N/P: Cycle colonies", 150, 160, 180);
    
    // Controls
    gui_renderer_draw_text(renderer, 15, 86, "Space: Pause/Resume", 150, 160, 180);
    gui_renderer_draw_text(renderer, 15, 98, "+/-: Sim speed", 150, 160, 180);
    
    // Toggles
    gui_renderer_draw_text(renderer, 15, 114, "G: Grid  I: Info panel", 150, 160, 180);
    gui_renderer_draw_text(renderer, 15, 126, "H: Hide this  Q: Quit", 150, 160, 180);
}
