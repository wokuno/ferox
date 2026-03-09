#include "renderer.h"
#include "../shared/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <math.h>

#define INITIAL_BUFFER_SIZE (1024 * 64)  // 64KB initial buffer
#define STATUS_BAR_HEIGHT 3
#define INFO_PANEL_WIDTH 30

void renderer_get_terminal_size(int* width, int* height) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *width = ws.ws_col;
        *height = ws.ws_row;
    } else {
        // Default fallback
        *width = 80;
        *height = 24;
    }
}

Renderer* renderer_create(void) {
    Renderer* r = (Renderer*)malloc(sizeof(Renderer));
    if (!r) return NULL;
    
    renderer_get_terminal_size(&r->term_width, &r->term_height);
    
    r->view_x = 0;
    r->view_y = 0;
    r->view_width = r->term_width - INFO_PANEL_WIDTH - 2;  // -2 for borders
    r->view_height = r->term_height - STATUS_BAR_HEIGHT - 2;
    r->selected_colony = 0;
    
    r->buffer_size = INITIAL_BUFFER_SIZE;
    r->buffer_used = 0;
    r->frame_buffer = (char*)malloc(r->buffer_size);
    if (!r->frame_buffer) {
        free(r);
        return NULL;
    }
    
    return r;
}

void renderer_destroy(Renderer* renderer) {
    if (!renderer) return;
    free(renderer->frame_buffer);
    free(renderer);
}

static void ensure_buffer_space(Renderer* r, size_t needed) {
    if (r->buffer_used + needed >= r->buffer_size) {
        size_t new_size = r->buffer_size * 2;
        while (new_size < r->buffer_used + needed) {
            new_size *= 2;
        }
        char* new_buf = (char*)realloc(r->frame_buffer, new_size);
        if (new_buf) {
            r->frame_buffer = new_buf;
            r->buffer_size = new_size;
        }
    }
}

void renderer_write(Renderer* renderer, const char* str) {
    size_t len = strlen(str);
    ensure_buffer_space(renderer, len + 1);
    memcpy(renderer->frame_buffer + renderer->buffer_used, str, len);
    renderer->buffer_used += len;
}

void renderer_writef(Renderer* renderer, const char* fmt, ...) {
    char temp[512];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);
    
    if (len > 0) {
        ensure_buffer_space(renderer, (size_t)len + 1);
        memcpy(renderer->frame_buffer + renderer->buffer_used, temp, (size_t)len);
        renderer->buffer_used += (size_t)len;
    }
}

int format_ansi_rgb_fg(char* buf, size_t size, uint8_t r, uint8_t g, uint8_t b) {
    return snprintf(buf, size, "\033[38;2;%d;%d;%dm", r, g, b);
}

int format_ansi_rgb_bg(char* buf, size_t size, uint8_t r, uint8_t g, uint8_t b) {
    return snprintf(buf, size, "\033[48;2;%d;%d;%dm", r, g, b);
}

void renderer_set_color_fg(Renderer* renderer, uint8_t r, uint8_t g, uint8_t b) {
    renderer_writef(renderer, "\033[38;2;%d;%d;%dm", r, g, b);
}

void renderer_set_color_bg(Renderer* renderer, uint8_t r, uint8_t g, uint8_t b) {
    renderer_writef(renderer, "\033[48;2;%d;%d;%dm", r, g, b);
}

void renderer_reset_colors(Renderer* renderer) {
    renderer_write(renderer, ANSI_RESET);
}

void renderer_move_cursor(Renderer* renderer, int row, int col) {
    renderer_writef(renderer, "\033[%d;%dH", row, col);
}

void renderer_clear(Renderer* renderer) {
    renderer->buffer_used = 0;
    renderer_write(renderer, ANSI_HIDE_CURSOR);
    renderer_write(renderer, ANSI_HOME);
    renderer_write(renderer, ANSI_CLEAR);
}

void renderer_draw_border(Renderer* renderer, int world_width, int world_height) {
    (void)world_width;
    (void)world_height;
    
    int petri_width = renderer->view_width + 2;
    int petri_height = renderer->view_height + 2;
    
    // Set border color (dark gray)
    renderer_set_color_fg(renderer, 100, 100, 100);
    
    // Top border
    renderer_move_cursor(renderer, 1, 1);
    renderer_write(renderer, BOX_TOP_LEFT);
    for (int i = 0; i < petri_width - 2; i++) {
        renderer_write(renderer, BOX_HORIZONTAL);
    }
    renderer_write(renderer, BOX_T_DOWN);
    
    // Continue top for info panel
    for (int i = 0; i < INFO_PANEL_WIDTH - 1; i++) {
        renderer_write(renderer, BOX_HORIZONTAL);
    }
    renderer_write(renderer, BOX_TOP_RIGHT);
    
    // Side borders
    for (int y = 2; y <= petri_height; y++) {
        renderer_move_cursor(renderer, y, 1);
        renderer_write(renderer, BOX_VERTICAL);
        
        renderer_move_cursor(renderer, y, petri_width);
        renderer_write(renderer, BOX_VERTICAL);
        
        renderer_move_cursor(renderer, y, petri_width + INFO_PANEL_WIDTH);
        renderer_write(renderer, BOX_VERTICAL);
    }
    
    // Bottom border of petri dish
    renderer_move_cursor(renderer, petri_height + 1, 1);
    renderer_write(renderer, BOX_BOTTOM_LEFT);
    for (int i = 0; i < petri_width - 2; i++) {
        renderer_write(renderer, BOX_HORIZONTAL);
    }
    renderer_write(renderer, BOX_T_UP);
    
    // Continue bottom for info panel
    for (int i = 0; i < INFO_PANEL_WIDTH - 1; i++) {
        renderer_write(renderer, BOX_HORIZONTAL);
    }
    renderer_write(renderer, BOX_BOTTOM_RIGHT);
    
    renderer_reset_colors(renderer);
}

void renderer_draw_cell(Renderer* renderer, int x, int y, uint8_t r, uint8_t g, uint8_t b, bool is_border) {
    // Calculate screen position (offset by border)
    int screen_x = x - renderer->view_x + 2;
    int screen_y = y - renderer->view_y + 2;
    
    // Bounds check
    if (screen_x < 2 || screen_x > renderer->view_width + 1 ||
        screen_y < 2 || screen_y > renderer->view_height + 1) {
        return;
    }
    
    renderer_move_cursor(renderer, screen_y, screen_x);
    renderer_set_color_fg(renderer, r, g, b);
    
    if (is_border) {
        // Dimmer color for border cells
        renderer_write(renderer, CELL_BORDER);
    } else {
        renderer_write(renderer, CELL_COLONY);
    }
    
    renderer_reset_colors(renderer);
}

void renderer_draw_world(Renderer* renderer, const ProtoWorld* world) {
    if (!world) return;
    
    // Clear background of petri dish (dark gray for "agar")
    renderer_set_color_bg(renderer, 20, 20, 25);
    for (int y = 2; y <= renderer->view_height + 1; y++) {
        renderer_move_cursor(renderer, y, 2);
        for (int x = 0; x < renderer->view_width; x++) {
            renderer_write(renderer, " ");
        }
    }
    renderer_reset_colors(renderer);
    
    // Draw each colony
    for (uint32_t i = 0; i < world->colony_count; i++) {
        const ProtoColony* colony = &world->colonies[i];
        if (!colony->alive) continue;
        
        // Calculate colony's visual representation based on radius
        // Use rounding instead of truncation to reduce jitter when centroid is near integer boundary
        int cx = (int)(colony->x + 0.5f);
        int cy = (int)(colony->y + 0.5f);
        float base_radius = colony->radius;
        
        // Draw cells with procedurally generated organic shape
        int int_radius = (int)(base_radius * 1.7f + 2);  // Extra margin for shape variation
        for (int dy = -int_radius; dy <= int_radius; dy++) {
            for (int dx = -int_radius; dx <= int_radius; dx++) {
                // Calculate angle from center
                float angle = atan2f((float)dy, (float)dx);
                if (angle < 0) angle += 2.0f * 3.14159265f;
                
                // Get shape multiplier from procedural noise
                float shape_mult = colony_shape_at_angle(colony->shape_seed, angle, colony->wobble_phase);
                
                // Calculate effective radius at this angle
                float effective_radius = base_radius * shape_mult;
                
                float dist = sqrtf((float)(dx * dx + dy * dy));
                
                if (dist <= effective_radius) {
                    int cell_x = cx + dx;
                    int cell_y = cy + dy;
                    
                    // Is this cell on the border? (within 1.2 units of edge)
                    bool is_border = dist > (effective_radius - 1.2f);
                    
                    // Highlight selected colony
                    uint8_t r = colony->color_r;
                    uint8_t g = colony->color_g;
                    uint8_t b = colony->color_b;
                    
                    if (colony->id == renderer->selected_colony) {
                        // Brighten selected colony
                        r = (uint8_t)(r + (255 - r) / 3);
                        g = (uint8_t)(g + (255 - g) / 3);
                        b = (uint8_t)(b + (255 - b) / 3);
                    }
                    
                    renderer_draw_cell(renderer, cell_x, cell_y, r, g, b, is_border);
                }
            }
        }
    }
}

static const char* renderer_state_name(uint8_t state) {
    switch (state) {
        case 1: return "Dormant";
        case 2: return "Stressed";
        case 0:
        default:
            return "Normal";
    }
}

static const char* renderer_behavior_mode_name(uint8_t mode) {
    switch (mode) {
        case PROTO_COLONY_BEHAVIOR_MODE_EXPANDING: return "Expanding";
        case PROTO_COLONY_BEHAVIOR_MODE_RAIDING: return "Raiding";
        case PROTO_COLONY_BEHAVIOR_MODE_FORTIFYING: return "Fortifying";
        case PROTO_COLONY_BEHAVIOR_MODE_COOPERATING: return "Cooperating";
        case PROTO_COLONY_BEHAVIOR_MODE_SURVIVAL: return "Survival";
        case PROTO_COLONY_BEHAVIOR_MODE_DORMANT: return "Dormant";
        case PROTO_COLONY_BEHAVIOR_MODE_BALANCED:
        default:
            return "Balanced";
    }
}

static const char* renderer_direction_name(int dir) {
    static const char* names[PROTO_DIRECTION_COUNT] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return (dir >= 0 && dir < PROTO_DIRECTION_COUNT) ? names[dir] : "--";
}

static const char* renderer_sensor_name(uint8_t sensor) {
    switch (sensor) {
        case PROTO_COLONY_SENSOR_NUTRIENT: return "Nutrient";
        case PROTO_COLONY_SENSOR_TOXIN: return "Toxin";
        case PROTO_COLONY_SENSOR_SIGNAL: return "Signal";
        case PROTO_COLONY_SENSOR_ALARM: return "Alarm";
        case PROTO_COLONY_SENSOR_FRONTIER: return "Frontier";
        case PROTO_COLONY_SENSOR_PRESSURE: return "Pressure";
        case PROTO_COLONY_SENSOR_MOMENTUM: return "Momentum";
        case PROTO_COLONY_SENSOR_GROWTH: return "Growth";
        default: return "Unknown";
    }
}

static const char* renderer_drive_name(uint8_t drive) {
    switch (drive) {
        case PROTO_COLONY_DRIVE_GROWTH: return "Growth";
        case PROTO_COLONY_DRIVE_CAUTION: return "Caution";
        case PROTO_COLONY_DRIVE_HOSTILITY: return "Hostility";
        case PROTO_COLONY_DRIVE_COHESION: return "Cohesion";
        case PROTO_COLONY_DRIVE_EXPLORATION: return "Explore";
        case PROTO_COLONY_DRIVE_PRESERVATION: return "Preserve";
        default: return "Unknown";
    }
}

static const char* renderer_action_name(uint8_t action) {
    switch (action) {
        case PROTO_COLONY_ACTION_EXPAND: return "Expand";
        case PROTO_COLONY_ACTION_ATTACK: return "Attack";
        case PROTO_COLONY_ACTION_DEFEND: return "Defend";
        case PROTO_COLONY_ACTION_SIGNAL: return "Signal";
        case PROTO_COLONY_ACTION_TRANSFER: return "Transfer";
        case PROTO_COLONY_ACTION_DORMANCY: return "Dormant";
        case PROTO_COLONY_ACTION_MOTILITY: return "Motility";
        default: return "Unknown";
    }
}

static float renderer_action_value(const ProtoColonyDetail* detail, uint8_t action) {
    switch (action) {
        case PROTO_COLONY_ACTION_EXPAND: return detail->action_expand;
        case PROTO_COLONY_ACTION_ATTACK: return detail->action_attack;
        case PROTO_COLONY_ACTION_DEFEND: return detail->action_defend;
        case PROTO_COLONY_ACTION_SIGNAL: return detail->action_signal;
        case PROTO_COLONY_ACTION_TRANSFER: return detail->action_transfer;
        case PROTO_COLONY_ACTION_DORMANCY: return detail->action_dormancy;
        case PROTO_COLONY_ACTION_MOTILITY: return detail->action_motility;
        default: return 0.0f;
    }
}

static void renderer_pick_top_actions(const ProtoColonyDetail* detail,
                                      uint8_t* first,
                                      uint8_t* second,
                                      uint8_t* third) {
    uint8_t order[PROTO_COLONY_ACTION_MOTILITY + 1] = {
        PROTO_COLONY_ACTION_EXPAND,
        PROTO_COLONY_ACTION_ATTACK,
        PROTO_COLONY_ACTION_DEFEND,
        PROTO_COLONY_ACTION_SIGNAL,
        PROTO_COLONY_ACTION_TRANSFER,
        PROTO_COLONY_ACTION_DORMANCY,
        PROTO_COLONY_ACTION_MOTILITY,
    };

    for (int i = 0; i < 7; i++) {
        for (int j = i + 1; j < 7; j++) {
            if (renderer_action_value(detail, order[j]) > renderer_action_value(detail, order[i])) {
                uint8_t tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    if (first) *first = order[0];
    if (second) *second = order[1];
    if (third) *third = order[2];
}

void renderer_draw_colony_info(Renderer* renderer, const ProtoColony* colony, const ProtoColonyDetail* detail) {
    int panel_x = renderer->view_width + 4;
    int panel_y = 3;
    const bool has_detail = detail && colony && detail->base.id == colony->id && detail->base.alive;
    
    renderer_set_color_fg(renderer, 200, 200, 200);
    
    renderer_move_cursor(renderer, panel_y++, panel_x);
    renderer_write(renderer, ANSI_BOLD "Colony Info" ANSI_RESET);
    renderer_set_color_fg(renderer, 200, 200, 200);
    
    // Only display info for alive colonies
    if (!colony || !colony->alive) {
        renderer_move_cursor(renderer, panel_y++, panel_x);
        renderer_write(renderer, "(none selected)");
        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_set_color_fg(renderer, 128, 128, 128);
        renderer_write(renderer, "Press TAB to cycle");
        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_write(renderer, "through colonies");
        renderer_reset_colors(renderer);
        return;
    }
    
    renderer_move_cursor(renderer, ++panel_y, panel_x);
    renderer_set_color_fg(renderer, colony->color_r, colony->color_g, colony->color_b);
    renderer_writef(renderer, CELL_COLONY " %s", colony->name);
    renderer_reset_colors(renderer);
    renderer_set_color_fg(renderer, 200, 200, 200);
    
    renderer_move_cursor(renderer, ++panel_y, panel_x);
    renderer_writef(renderer, "ID: %u", colony->id);
    
    renderer_move_cursor(renderer, ++panel_y, panel_x);
    renderer_writef(renderer, "Population: %u", colony->population);
    
    renderer_move_cursor(renderer, ++panel_y, panel_x);
    renderer_writef(renderer, "Peak: %u", colony->max_population);
    
    renderer_move_cursor(renderer, ++panel_y, panel_x);
    renderer_writef(renderer, "Position: (%.1f, %.1f)", colony->x, colony->y);
    
    renderer_move_cursor(renderer, ++panel_y, panel_x);
    renderer_writef(renderer, "Radius: %.1f", colony->radius);
    
    renderer_move_cursor(renderer, ++panel_y, panel_x);
    renderer_writef(renderer, "Spread: %.2f", colony->growth_rate);

    if (has_detail) {
        uint8_t top_action = 0;
        uint8_t second_action = 0;
        uint8_t third_action = 0;
        renderer_pick_top_actions(detail, &top_action, &second_action, &third_action);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "State: %s%s",
                        renderer_state_name(detail->state),
                        (detail->flags & COLONY_DETAIL_FLAG_DORMANT) ? " (spore)" : "");

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Mode: %s %s",
                        renderer_behavior_mode_name(detail->behavior_mode),
                        renderer_direction_name(detail->focus_direction));

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Stress: %.0f%%  Biofilm: %.0f%%",
                        detail->stress_level * 100.0f,
                        detail->biofilm_strength * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Signal: %.0f%%  Age: %u",
                        detail->signal_strength * 100.0f,
                        detail->age);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Drift: %+0.2f, %+0.2f", detail->drift_x, detail->drift_y);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Acts: X %.0f A %.0f D %.0f S %.0f",
                        detail->action_expand * 100.0f,
                        detail->action_attack * 100.0f,
                        detail->action_defend * 100.0f,
                        detail->action_signal * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "More: T %.0f Z %.0f M %.0f",
                        detail->action_transfer * 100.0f,
                        detail->action_dormancy * 100.0f,
                        detail->action_motility * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Top: %s %.0f %s %.0f %s %.0f",
                        renderer_action_name(top_action),
                        renderer_action_value(detail, top_action) * 100.0f,
                        renderer_action_name(second_action),
                        renderer_action_value(detail, second_action) * 100.0f,
                        renderer_action_name(third_action),
                        renderer_action_value(detail, third_action) * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Drive: %s %.0f%%",
                        renderer_drive_name(detail->dominant_drive),
                        detail->dominant_drive_value * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "2nd:   %s %.0f%%",
                        renderer_drive_name(detail->secondary_drive),
                        detail->secondary_drive_value * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Sense: %s %.0f%%",
                        renderer_sensor_name(detail->dominant_sensor),
                        detail->dominant_sensor_value * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "2nd:   %s %.0f%%",
                        renderer_sensor_name(detail->secondary_sensor),
                        detail->secondary_sensor_value * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "S->D: %s>%s %+0.2f",
                        renderer_sensor_name(detail->sensor_link_sensor),
                        renderer_drive_name(detail->sensor_link_drive),
                        detail->sensor_link_value);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "D->A: %s>%s %+0.2f",
                        renderer_drive_name(detail->action_link_drive),
                        renderer_action_name(detail->action_link_action),
                        detail->action_link_value);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "2nd: %s>%s %+0.2f",
                        renderer_sensor_name(detail->secondary_sensor_link_sensor),
                        renderer_drive_name(detail->secondary_sensor_link_drive),
                        detail->secondary_sensor_link_value);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "2nd: %s>%s %+0.2f",
                        renderer_drive_name(detail->secondary_action_link_drive),
                        renderer_action_name(detail->secondary_action_link_action),
                        detail->secondary_action_link_value);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_write(renderer, "Character:");

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Expand %.0f  Aggro %.0f",
                        detail->trait_expansion * 100.0f,
                        detail->trait_aggression * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Resil  %.0f  Coop  %.0f",
                        detail->trait_resilience * 100.0f,
                        detail->trait_cooperation * 100.0f);

        renderer_move_cursor(renderer, ++panel_y, panel_x);
        renderer_writef(renderer, "Eff    %.0f  Learn %.0f",
                        detail->trait_efficiency * 100.0f,
                        detail->trait_learning * 100.0f);
    }
    
    renderer_reset_colors(renderer);
}

void renderer_draw_status(Renderer* renderer, uint32_t tick, int colony_count, bool paused, float speed) {
    int status_y = renderer->view_height + 4;
    
    // Status bar background
    renderer_set_color_bg(renderer, 40, 40, 50);
    renderer_move_cursor(renderer, status_y, 1);
    for (int i = 0; i < renderer->term_width; i++) {
        renderer_write(renderer, " ");
    }
    
    // Status text
    renderer_move_cursor(renderer, status_y, 2);
    renderer_set_color_fg(renderer, 100, 200, 100);
    renderer_write(renderer, "FEROX ");
    
    renderer_set_color_fg(renderer, 200, 200, 200);
    renderer_writef(renderer, "Tick: %u  ", tick);
    renderer_writef(renderer, "Colonies: %d  ", colony_count);
    renderer_writef(renderer, "Speed: %.1fx  ", speed);
    
    if (paused) {
        renderer_set_color_fg(renderer, 255, 200, 100);
        renderer_write(renderer, "[PAUSED]  ");
    }
    
    // Controls hint
    renderer_set_color_fg(renderer, 128, 128, 128);
    renderer_write(renderer, "Q:Quit  P:Pause  +/-:Speed  Arrows:Scroll  TAB:Select");
    
    renderer_reset_colors(renderer);
}

void renderer_present(Renderer* renderer) {
    // Write the entire frame buffer at once
    if (renderer->buffer_used > 0) {
        renderer->frame_buffer[renderer->buffer_used] = '\0';
        fputs(renderer->frame_buffer, stdout);
        fflush(stdout);
    }
}

void renderer_scroll(Renderer* renderer, int dx, int dy) {
    renderer->view_x += dx;
    renderer->view_y += dy;
    
    // Clamp to valid range
    if (renderer->view_x < 0) renderer->view_x = 0;
    if (renderer->view_y < 0) renderer->view_y = 0;
}

void renderer_center_on(Renderer* renderer, int x, int y) {
    renderer->view_x = x - renderer->view_width / 2;
    renderer->view_y = y - renderer->view_height / 2;
    
    if (renderer->view_x < 0) renderer->view_x = 0;
    if (renderer->view_y < 0) renderer->view_y = 0;
}
