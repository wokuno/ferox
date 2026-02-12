/**
 * @file test_gui.c
 * @brief Unit tests for GUI components (renderer logic, input, client)
 * 
 * Tests GUI logic without requiring an actual SDL display.
 * Uses mock/minimal state objects to test coordinate conversion,
 * zoom/pan calculations, selection logic, and input state.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>

// We test the logic portions without SDL dependencies
// Define minimal structures for testing

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Mock structures for testing without SDL
// ============================================================================

typedef struct MockRenderer {
    int window_width;
    int window_height;
    float view_x, view_y;
    float zoom;
    int show_grid;
    int show_info_panel;
    uint32_t selected_colony;
    float time;
} MockRenderer;

typedef struct MockColony {
    uint32_t id;
    float x, y;
    float radius;
    int alive;
    char name[64];
} MockColony;

typedef struct MockWorld {
    uint32_t colony_count;
    MockColony colonies[100];
    int width, height;
} MockWorld;

typedef struct MockClient {
    MockRenderer renderer;
    MockWorld world;
    int connected;
    uint32_t selected_colony;
    uint32_t selected_index;
} MockClient;

// ============================================================================
// Re-implement key functions for testing
// ============================================================================

void mock_world_to_screen(MockRenderer* r, float wx, float wy, int* sx, int* sy) {
    float cx = r->window_width / 2.0f;
    float cy = r->window_height / 2.0f;
    *sx = (int)(cx + (wx - r->view_x) * r->zoom);
    *sy = (int)(cy + (wy - r->view_y) * r->zoom);
}

void mock_screen_to_world(MockRenderer* r, int sx, int sy, float* wx, float* wy) {
    float cx = r->window_width / 2.0f;
    float cy = r->window_height / 2.0f;
    *wx = r->view_x + (sx - cx) / r->zoom;
    *wy = r->view_y + (sy - cy) / r->zoom;
}

void mock_set_zoom(MockRenderer* r, float zoom) {
    r->zoom = zoom;
    if (r->zoom < 1.0f) r->zoom = 1.0f;
    if (r->zoom > 50.0f) r->zoom = 50.0f;
}

void mock_pan(MockRenderer* r, float dx, float dy) {
    r->view_x += dx;
    r->view_y += dy;
}

void mock_center_on(MockRenderer* r, float x, float y) {
    r->view_x = x;
    r->view_y = y;
}

void mock_zoom_at(MockRenderer* r, int screen_x, int screen_y, float factor) {
    float world_x, world_y;
    mock_screen_to_world(r, screen_x, screen_y, &world_x, &world_y);
    
    float new_zoom = r->zoom * factor;
    if (new_zoom < 1.0f) new_zoom = 1.0f;
    if (new_zoom > 50.0f) new_zoom = 50.0f;
    r->zoom = new_zoom;
    
    float new_world_x, new_world_y;
    mock_screen_to_world(r, screen_x, screen_y, &new_world_x, &new_world_y);
    
    r->view_x += world_x - new_world_x;
    r->view_y += world_y - new_world_y;
}

void mock_toggle_grid(MockRenderer* r) {
    r->show_grid = !r->show_grid;
}

void mock_toggle_info_panel(MockRenderer* r) {
    r->show_info_panel = !r->show_info_panel;
}

void mock_update_time(MockRenderer* r, float dt) {
    r->time += dt;
}

void mock_select_next_colony(MockClient* client) {
    if (client->world.colony_count == 0) {
        client->selected_colony = 0;
        client->selected_index = 0;
        return;
    }
    
    uint32_t start = client->selected_index;
    uint32_t count = client->world.colony_count;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start + i + 1) % count;
        if (i == 0 && client->selected_colony == 0) {
            idx = 0;
        }
        if (client->world.colonies[idx].alive) {
            client->selected_index = idx;
            client->selected_colony = client->world.colonies[idx].id;
            mock_center_on(&client->renderer, 
                          client->world.colonies[idx].x,
                          client->world.colonies[idx].y);
            return;
        }
    }
    client->selected_colony = 0;
}

void mock_select_prev_colony(MockClient* client) {
    if (client->world.colony_count == 0) {
        client->selected_colony = 0;
        client->selected_index = 0;
        return;
    }
    
    uint32_t start = client->selected_index;
    uint32_t count = client->world.colony_count;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start + count - i - 1) % count;
        if (client->world.colonies[idx].alive) {
            client->selected_index = idx;
            client->selected_colony = client->world.colonies[idx].id;
            mock_center_on(&client->renderer,
                          client->world.colonies[idx].x,
                          client->world.colonies[idx].y);
            return;
        }
    }
    client->selected_colony = 0;
}

void mock_select_colony_at(MockClient* client, float wx, float wy) {
    for (uint32_t i = 0; i < client->world.colony_count; i++) {
        MockColony* c = &client->world.colonies[i];
        if (!c->alive) continue;
        
        float dx = wx - c->x;
        float dy = wy - c->y;
        float dist = sqrtf(dx*dx + dy*dy);
        
        if (dist <= c->radius * 1.5f) {
            client->selected_index = i;
            client->selected_colony = c->id;
            mock_center_on(&client->renderer, c->x, c->y);
            return;
        }
    }
}

void mock_deselect(MockClient* client) {
    client->selected_colony = 0;
}

// ============================================================================
// Test framework
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))
#define ASSERT_INT_EQ(a, b) ASSERT((a) == (b))

// ============================================================================
// Coordinate Conversion Tests
// ============================================================================

TEST(test_coord_world_to_screen_maps_center) {
    MockRenderer r = {.window_width = 800, .window_height = 600, 
                      .view_x = 50.0f, .view_y = 50.0f, .zoom = 10.0f};
    int sx, sy;
    mock_world_to_screen(&r, 50.0f, 50.0f, &sx, &sy);
    // Center of view should map to center of screen
    ASSERT_INT_EQ(sx, 400);
    ASSERT_INT_EQ(sy, 300);
}

TEST(test_coord_world_to_screen_applies_offset) {
    MockRenderer r = {.window_width = 800, .window_height = 600,
                      .view_x = 50.0f, .view_y = 50.0f, .zoom = 10.0f};
    int sx, sy;
    // 10 world units right of center = 100 pixels right of center
    mock_world_to_screen(&r, 60.0f, 50.0f, &sx, &sy);
    ASSERT_INT_EQ(sx, 500);
    ASSERT_INT_EQ(sy, 300);
}

TEST(test_coord_screen_to_world_maps_center) {
    MockRenderer r = {.window_width = 800, .window_height = 600,
                      .view_x = 50.0f, .view_y = 50.0f, .zoom = 10.0f};
    float wx, wy;
    mock_screen_to_world(&r, 400, 300, &wx, &wy);
    ASSERT_FLOAT_EQ(wx, 50.0f, 0.01f);
    ASSERT_FLOAT_EQ(wy, 50.0f, 0.01f);
}

TEST(test_coord_screen_to_world_applies_offset) {
    MockRenderer r = {.window_width = 800, .window_height = 600,
                      .view_x = 50.0f, .view_y = 50.0f, .zoom = 10.0f};
    float wx, wy;
    // 100 pixels right of center = 10 world units right
    mock_screen_to_world(&r, 500, 300, &wx, &wy);
    ASSERT_FLOAT_EQ(wx, 60.0f, 0.01f);
    ASSERT_FLOAT_EQ(wy, 50.0f, 0.01f);
}

TEST(test_coord_roundtrip_preserves_position) {
    MockRenderer r = {.window_width = 1280, .window_height = 720,
                      .view_x = 100.0f, .view_y = 75.0f, .zoom = 6.0f};
    float wx = 123.456f, wy = 78.901f;
    int sx, sy;
    float wx2, wy2;
    
    mock_world_to_screen(&r, wx, wy, &sx, &sy);
    mock_screen_to_world(&r, sx, sy, &wx2, &wy2);
    
    // Should be within 1/zoom units due to integer truncation
    ASSERT_FLOAT_EQ(wx, wx2, 1.0f/r.zoom + 0.01f);
    ASSERT_FLOAT_EQ(wy, wy2, 1.0f/r.zoom + 0.01f);
}

TEST(test_coord_zoom_scales_offset) {
    MockRenderer r = {.window_width = 800, .window_height = 600,
                      .view_x = 0.0f, .view_y = 0.0f, .zoom = 1.0f};
    int sx1, sy1, sx2, sy2;
    
    mock_world_to_screen(&r, 10.0f, 10.0f, &sx1, &sy1);
    r.zoom = 2.0f;
    mock_world_to_screen(&r, 10.0f, 10.0f, &sx2, &sy2);
    
    // At higher zoom, offset from center should be doubled
    int offset1 = sx1 - 400;
    int offset2 = sx2 - 400;
    ASSERT_INT_EQ(offset2, offset1 * 2);
}

// ============================================================================
// Zoom Tests
// ============================================================================

TEST(test_zoom_set_accepts_valid_values) {
    MockRenderer r = {.zoom = 6.0f};
    mock_set_zoom(&r, 12.0f);
    ASSERT_FLOAT_EQ(r.zoom, 12.0f, 0.01f);
}

TEST(test_zoom_clamps_to_minimum_one) {
    MockRenderer r = {.zoom = 6.0f};
    mock_set_zoom(&r, 0.1f);
    ASSERT_FLOAT_EQ(r.zoom, 1.0f, 0.01f);
}

TEST(test_zoom_clamps_to_maximum_fifty) {
    MockRenderer r = {.zoom = 6.0f};
    mock_set_zoom(&r, 100.0f);
    ASSERT_FLOAT_EQ(r.zoom, 50.0f, 0.01f);
}

TEST(test_zoom_at_keeps_point_stable) {
    MockRenderer r = {.window_width = 800, .window_height = 600,
                      .view_x = 50.0f, .view_y = 50.0f, .zoom = 10.0f};
    
    // Pick a point on screen
    int screen_x = 200, screen_y = 150;
    float world_x_before, world_y_before;
    mock_screen_to_world(&r, screen_x, screen_y, &world_x_before, &world_y_before);
    
    // Zoom in at that point
    mock_zoom_at(&r, screen_x, screen_y, 2.0f);
    
    // Same screen point should map to same world point
    float world_x_after, world_y_after;
    mock_screen_to_world(&r, screen_x, screen_y, &world_x_after, &world_y_after);
    
    ASSERT_FLOAT_EQ(world_x_before, world_x_after, 0.1f);
    ASSERT_FLOAT_EQ(world_y_before, world_y_after, 0.1f);
}

TEST(test_zoom_at_center_does_not_pan) {
    MockRenderer r = {.window_width = 800, .window_height = 600,
                      .view_x = 50.0f, .view_y = 50.0f, .zoom = 10.0f};
    
    float view_x_before = r.view_x;
    float view_y_before = r.view_y;
    
    // Zoom at center of screen
    mock_zoom_at(&r, 400, 300, 2.0f);
    
    // View position should not change
    ASSERT_FLOAT_EQ(r.view_x, view_x_before, 0.01f);
    ASSERT_FLOAT_EQ(r.view_y, view_y_before, 0.01f);
}

// ============================================================================
// Pan Tests
// ============================================================================

TEST(test_pan_moves_view_horizontally) {
    MockRenderer r = {.view_x = 50.0f, .view_y = 50.0f};
    mock_pan(&r, 10.0f, 0.0f);
    ASSERT_FLOAT_EQ(r.view_x, 60.0f, 0.01f);
    ASSERT_FLOAT_EQ(r.view_y, 50.0f, 0.01f);
}

TEST(test_pan_moves_view_diagonally) {
    MockRenderer r = {.view_x = 0.0f, .view_y = 0.0f};
    mock_pan(&r, 5.0f, -3.0f);
    ASSERT_FLOAT_EQ(r.view_x, 5.0f, 0.01f);
    ASSERT_FLOAT_EQ(r.view_y, -3.0f, 0.01f);
}

TEST(test_center_on_sets_view_position) {
    MockRenderer r = {.view_x = 100.0f, .view_y = 200.0f};
    mock_center_on(&r, 25.0f, 75.0f);
    ASSERT_FLOAT_EQ(r.view_x, 25.0f, 0.01f);
    ASSERT_FLOAT_EQ(r.view_y, 75.0f, 0.01f);
}

// ============================================================================
// Toggle Tests
// ============================================================================

TEST(test_toggle_grid_toggles_state) {
    MockRenderer r = {.show_grid = 0};
    mock_toggle_grid(&r);
    ASSERT(r.show_grid == 1);
    mock_toggle_grid(&r);
    ASSERT(r.show_grid == 0);
}

TEST(test_toggle_info_panel_toggles_state) {
    MockRenderer r = {.show_info_panel = 1};
    mock_toggle_info_panel(&r);
    ASSERT(r.show_info_panel == 0);
    mock_toggle_info_panel(&r);
    ASSERT(r.show_info_panel == 1);
}

// ============================================================================
// Time/Animation Tests
// ============================================================================

TEST(test_animation_time_increments) {
    MockRenderer r = {.time = 0.0f};
    mock_update_time(&r, 0.016f);
    ASSERT_FLOAT_EQ(r.time, 0.016f, 0.0001f);
    mock_update_time(&r, 0.016f);
    ASSERT_FLOAT_EQ(r.time, 0.032f, 0.0001f);
}

TEST(test_animation_time_accumulates) {
    MockRenderer r = {.time = 5.0f};
    for (int i = 0; i < 100; i++) {
        mock_update_time(&r, 0.01f);
    }
    ASSERT_FLOAT_EQ(r.time, 6.0f, 0.01f);
}

// ============================================================================
// Colony Selection Tests
// ============================================================================

static MockClient create_test_client_with_colonies(int count) {
    MockClient client = {0};
    client.renderer.window_width = 800;
    client.renderer.window_height = 600;
    client.renderer.view_x = 50.0f;
    client.renderer.view_y = 50.0f;
    client.renderer.zoom = 6.0f;
    client.world.colony_count = count;
    client.world.width = 100;
    client.world.height = 100;
    
    for (int i = 0; i < count && i < 100; i++) {
        client.world.colonies[i].id = i + 1;
        client.world.colonies[i].x = 10.0f + i * 15.0f;
        client.world.colonies[i].y = 10.0f + i * 10.0f;
        client.world.colonies[i].radius = 5.0f;
        client.world.colonies[i].alive = 1;
        snprintf(client.world.colonies[i].name, 64, "Colony %d", i + 1);
    }
    return client;
}

TEST(test_selection_empty_world_returns_zero) {
    MockClient client = {0};
    client.world.colony_count = 0;
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 0);
}

TEST(test_selection_finds_single_colony) {
    MockClient client = create_test_client_with_colonies(1);
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 1);
    ASSERT_INT_EQ(client.selected_index, 0);
}

TEST(test_selection_cycles_through_colonies) {
    MockClient client = create_test_client_with_colonies(3);
    
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 1);
    
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 2);
    
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 3);
    
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 1);  // Wraps around
}

TEST(test_selection_prev_cycles_backward) {
    MockClient client = create_test_client_with_colonies(3);
    
    mock_select_prev_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 3);
    
    mock_select_prev_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 2);
    
    mock_select_prev_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 1);
    
    mock_select_prev_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 3);  // Wraps around
}

TEST(test_selection_skips_dead_colonies) {
    MockClient client = create_test_client_with_colonies(5);
    client.world.colonies[1].alive = 0;  // Colony 2 dead
    client.world.colonies[2].alive = 0;  // Colony 3 dead
    
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 1);
    
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 4);  // Skipped 2 and 3
    
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 5);
}

TEST(test_selection_centers_view_on_colony) {
    MockClient client = create_test_client_with_colonies(3);
    client.renderer.view_x = 0.0f;
    client.renderer.view_y = 0.0f;
    
    mock_select_next_colony(&client);
    
    // View should center on first colony
    ASSERT_FLOAT_EQ(client.renderer.view_x, client.world.colonies[0].x, 0.01f);
    ASSERT_FLOAT_EQ(client.renderer.view_y, client.world.colonies[0].y, 0.01f);
}

TEST(test_deselect_clears_selection) {
    MockClient client = create_test_client_with_colonies(3);
    mock_select_next_colony(&client);
    ASSERT(client.selected_colony != 0);
    
    mock_deselect(&client);
    ASSERT_INT_EQ(client.selected_colony, 0);
}

TEST(test_selection_at_position_finds_colony) {
    MockClient client = create_test_client_with_colonies(3);
    // Colony 0 is at (10, 10), colony 1 at (25, 20), colony 2 at (40, 30)
    
    // Click near colony 2
    mock_select_colony_at(&client, 41.0f, 31.0f);
    ASSERT_INT_EQ(client.selected_colony, 3);
    
    // Click near colony 0
    mock_select_colony_at(&client, 10.0f, 10.0f);
    ASSERT_INT_EQ(client.selected_colony, 1);
}

TEST(test_selection_miss_does_not_select) {
    MockClient client = create_test_client_with_colonies(3);
    client.selected_colony = 0;
    
    // Click far from any colony
    mock_select_colony_at(&client, 200.0f, 200.0f);
    ASSERT_INT_EQ(client.selected_colony, 0);
}

TEST(test_selection_ignores_dead_colonies) {
    MockClient client = create_test_client_with_colonies(3);
    client.world.colonies[0].alive = 0;
    
    // Click on dead colony position
    mock_select_colony_at(&client, 10.0f, 10.0f);
    ASSERT_INT_EQ(client.selected_colony, 0);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(test_coord_conversion_at_origin) {
    MockRenderer r = {.window_width = 800, .window_height = 600,
                      .view_x = 0.0f, .view_y = 0.0f, .zoom = 10.0f};
    int sx, sy;
    float wx, wy;
    
    mock_world_to_screen(&r, 0.0f, 0.0f, &sx, &sy);
    ASSERT_INT_EQ(sx, 400);
    ASSERT_INT_EQ(sy, 300);
    
    mock_screen_to_world(&r, 400, 300, &wx, &wy);
    ASSERT_FLOAT_EQ(wx, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(wy, 0.0f, 0.01f);
}

TEST(test_coord_converts_negative_world) {
    MockRenderer r = {.window_width = 800, .window_height = 600,
                      .view_x = 0.0f, .view_y = 0.0f, .zoom = 10.0f};
    float wx, wy;
    
    // Top-left corner of screen
    mock_screen_to_world(&r, 0, 0, &wx, &wy);
    ASSERT_FLOAT_EQ(wx, -40.0f, 0.1f);  // 400/10 = 40 units left of center
    ASSERT_FLOAT_EQ(wy, -30.0f, 0.1f);  // 300/10 = 30 units above center
}

TEST(test_zoom_handles_extreme_levels) {
    MockRenderer r = {.window_width = 800, .window_height = 600,
                      .view_x = 50.0f, .view_y = 50.0f, .zoom = 1.0f};
    
    // At minimum zoom
    int sx1, sy1;
    mock_world_to_screen(&r, 100.0f, 100.0f, &sx1, &sy1);
    
    // At maximum zoom
    r.zoom = 50.0f;
    int sx2, sy2;
    mock_world_to_screen(&r, 100.0f, 100.0f, &sx2, &sy2);
    
    // Both should give valid screen coordinates
    ASSERT(sx1 >= 0 && sx1 <= 1600);
    ASSERT(sx2 >= -100000 && sx2 <= 100000);
}

TEST(test_pan_handles_extreme_values) {
    MockRenderer r = {.view_x = 0.0f, .view_y = 0.0f};
    mock_pan(&r, 1000000.0f, -1000000.0f);
    ASSERT_FLOAT_EQ(r.view_x, 1000000.0f, 1.0f);
    ASSERT_FLOAT_EQ(r.view_y, -1000000.0f, 1.0f);
}

TEST(test_selection_all_dead_returns_zero) {
    MockClient client = create_test_client_with_colonies(3);
    client.world.colonies[0].alive = 0;
    client.world.colonies[1].alive = 0;
    client.world.colonies[2].alive = 0;
    
    mock_select_next_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 0);
    
    mock_select_prev_colony(&client);
    ASSERT_INT_EQ(client.selected_colony, 0);
}

TEST(test_selection_cycles_100_colonies) {
    MockClient client = {0};
    client.world.colony_count = 100;
    for (int i = 0; i < 100; i++) {
        client.world.colonies[i].id = i + 1;
        client.world.colonies[i].alive = 1;
        client.world.colonies[i].x = (float)i;
        client.world.colonies[i].y = (float)i;
        client.world.colonies[i].radius = 1.0f;
    }
    
    // Should be able to cycle through all
    for (int i = 0; i < 100; i++) {
        mock_select_next_colony(&client);
        ASSERT_INT_EQ((int)client.selected_colony, i + 1);
    }
    
    // Should wrap
    mock_select_next_colony(&client);
    ASSERT_INT_EQ((int)client.selected_colony, 1);
}

// ============================================================================
// Input State Tests (mock structures only)
// ============================================================================

typedef enum {
    MOCK_INPUT_NONE,
    MOCK_INPUT_QUIT,
    MOCK_INPUT_PAUSE,
    MOCK_INPUT_ZOOM_IN,
    MOCK_INPUT_ZOOM_OUT,
    MOCK_INPUT_PAN_UP,
    MOCK_INPUT_PAN_DOWN,
    MOCK_INPUT_PAN_LEFT,
    MOCK_INPUT_PAN_RIGHT
} MockInputAction;

typedef struct {
    int mouse_x, mouse_y;
    int mouse_dx, mouse_dy;
    int mouse_left_down;
    int mouse_right_down;
    int scroll_delta;
    int shift_held;
    int ctrl_held;
    int clicked;
    int click_x, click_y;
    MockInputAction action;
} MockInputState;

TEST(test_input_state_initializes_to_defaults) {
    MockInputState state = {0};
    ASSERT_INT_EQ(state.action, MOCK_INPUT_NONE);
    ASSERT_INT_EQ(state.mouse_x, 0);
    ASSERT_INT_EQ(state.clicked, 0);
}

TEST(test_input_state_stores_click_position) {
    MockInputState state = {0};
    state.clicked = 1;
    state.click_x = 100;
    state.click_y = 200;
    
    ASSERT(state.clicked);
    ASSERT_INT_EQ(state.click_x, 100);
    ASSERT_INT_EQ(state.click_y, 200);
}

TEST(test_input_state_stores_modifier_keys) {
    MockInputState state = {0};
    state.shift_held = 1;
    state.ctrl_held = 1;
    
    ASSERT(state.shift_held);
    ASSERT(state.ctrl_held);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("=== GUI Unit Tests ===\n\n");
    
    printf("Coordinate Conversion Tests:\n");
    RUN_TEST(test_coord_world_to_screen_maps_center);
    RUN_TEST(test_coord_world_to_screen_applies_offset);
    RUN_TEST(test_coord_screen_to_world_maps_center);
    RUN_TEST(test_coord_screen_to_world_applies_offset);
    RUN_TEST(test_coord_roundtrip_preserves_position);
    RUN_TEST(test_coord_zoom_scales_offset);
    
    printf("\nZoom Tests:\n");
    RUN_TEST(test_zoom_set_accepts_valid_values);
    RUN_TEST(test_zoom_clamps_to_minimum_one);
    RUN_TEST(test_zoom_clamps_to_maximum_fifty);
    RUN_TEST(test_zoom_at_keeps_point_stable);
    RUN_TEST(test_zoom_at_center_does_not_pan);
    
    printf("\nPan Tests:\n");
    RUN_TEST(test_pan_moves_view_horizontally);
    RUN_TEST(test_pan_moves_view_diagonally);
    RUN_TEST(test_center_on_sets_view_position);
    
    printf("\nToggle Tests:\n");
    RUN_TEST(test_toggle_grid_toggles_state);
    RUN_TEST(test_toggle_info_panel_toggles_state);
    
    printf("\nAnimation Tests:\n");
    RUN_TEST(test_animation_time_increments);
    RUN_TEST(test_animation_time_accumulates);
    
    printf("\nColony Selection Tests:\n");
    RUN_TEST(test_selection_empty_world_returns_zero);
    RUN_TEST(test_selection_finds_single_colony);
    RUN_TEST(test_selection_cycles_through_colonies);
    RUN_TEST(test_selection_prev_cycles_backward);
    RUN_TEST(test_selection_skips_dead_colonies);
    RUN_TEST(test_selection_centers_view_on_colony);
    RUN_TEST(test_deselect_clears_selection);
    RUN_TEST(test_selection_at_position_finds_colony);
    RUN_TEST(test_selection_miss_does_not_select);
    RUN_TEST(test_selection_ignores_dead_colonies);
    
    printf("\nEdge Case Tests:\n");
    RUN_TEST(test_coord_conversion_at_origin);
    RUN_TEST(test_coord_converts_negative_world);
    RUN_TEST(test_zoom_handles_extreme_levels);
    RUN_TEST(test_pan_handles_extreme_values);
    RUN_TEST(test_selection_all_dead_returns_zero);
    RUN_TEST(test_selection_cycles_100_colonies);
    
    printf("\nInput State Tests:\n");
    RUN_TEST(test_input_state_initializes_to_defaults);
    RUN_TEST(test_input_state_stores_click_position);
    RUN_TEST(test_input_state_stores_modifier_keys);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    
    return (tests_passed == tests_run) ? 0 : 1;
}
