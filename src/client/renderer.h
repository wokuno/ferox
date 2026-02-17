#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include "../shared/protocol.h"

// ANSI escape codes
#define ANSI_RESET "\033[0m"
#define ANSI_CLEAR "\033[2J"
#define ANSI_HOME "\033[H"
#define ANSI_HIDE_CURSOR "\033[?25l"
#define ANSI_SHOW_CURSOR "\033[?25h"
#define ANSI_BOLD "\033[1m"
#define ANSI_DIM "\033[2m"

// Cursor movement
#define ANSI_MOVE_TO(row, col) "\033[" #row ";" #col "H"

// Box drawing characters (UTF-8)
#define BOX_HORIZONTAL "─"
#define BOX_VERTICAL "│"
#define BOX_TOP_LEFT "┌"
#define BOX_TOP_RIGHT "┐"
#define BOX_BOTTOM_LEFT "└"
#define BOX_BOTTOM_RIGHT "┘"
#define BOX_T_DOWN "┬"
#define BOX_T_UP "┴"
#define BOX_T_RIGHT "├"
#define BOX_T_LEFT "┤"
#define BOX_CROSS "┼"

// Cell characters
#define CELL_EMPTY " "
#define CELL_COLONY "●"
#define CELL_BORDER "○"

typedef struct Renderer {
    int term_width;
    int term_height;
    int view_x, view_y;      // Viewport offset into world
    int view_width, view_height;
    uint32_t selected_colony;
    char* frame_buffer;      // Pre-built frame for efficiency
    size_t buffer_size;
    size_t buffer_used;
} Renderer;

// Create and destroy
Renderer* renderer_create(void);
void renderer_destroy(Renderer* renderer);

// Rendering functions
void renderer_clear(Renderer* renderer);
void renderer_draw_world(Renderer* renderer, const proto_world* world);
void renderer_draw_world_grid(Renderer* renderer, const proto_world* world);
void renderer_draw_world_centroid(Renderer* renderer, const proto_world* world);
void renderer_draw_cell(Renderer* renderer, int x, int y, uint8_t r, uint8_t g, uint8_t b, bool is_border);
void renderer_draw_border(Renderer* renderer, int world_width, int world_height);
void renderer_draw_colony_info(Renderer* renderer, const proto_colony* colony);
void renderer_draw_status(Renderer* renderer, uint32_t tick, int colony_count, bool paused, float speed);
void renderer_present(Renderer* renderer);  // Output to terminal

// Viewport control
void renderer_scroll(Renderer* renderer, int dx, int dy);
void renderer_center_on(Renderer* renderer, int x, int y);
void renderer_get_terminal_size(int* width, int* height);

// Helper functions
void renderer_set_color_fg(Renderer* renderer, uint8_t r, uint8_t g, uint8_t b);
void renderer_set_color_bg(Renderer* renderer, uint8_t r, uint8_t g, uint8_t b);
void renderer_reset_colors(Renderer* renderer);
void renderer_move_cursor(Renderer* renderer, int row, int col);
void renderer_write(Renderer* renderer, const char* str);
void renderer_writef(Renderer* renderer, const char* fmt, ...);

// Format an RGB color escape sequence into a buffer
int format_ansi_rgb_fg(char* buf, size_t size, uint8_t r, uint8_t g, uint8_t b);
int format_ansi_rgb_bg(char* buf, size_t size, uint8_t r, uint8_t g, uint8_t b);

#endif // RENDERER_H
