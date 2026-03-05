#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/client/renderer.h"

#include "../src/client/renderer.c"

static int g_tests_run = 0;

static Renderer make_renderer(int view_width, int view_height, size_t buffer_size) {
    Renderer r;
    memset(&r, 0, sizeof(r));
    r.term_width = view_width + 40;
    r.term_height = view_height + 10;
    r.view_width = view_width;
    r.view_height = view_height;
    r.buffer_size = buffer_size;
    r.frame_buffer = (char*)calloc(1, buffer_size);
    assert(r.frame_buffer != NULL);
    return r;
}

static void free_renderer(Renderer* r) {
    free(r->frame_buffer);
    r->frame_buffer = NULL;
}

static int count_substring(const char* haystack, const char* needle) {
    int count = 0;
    const char* p = haystack;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;
    }
    return count;
}

static proto_world make_world(uint32_t width, uint32_t height) {
    proto_world world;
    memset(&world, 0, sizeof(world));
    world.width = width;
    world.height = height;
    return world;
}

static void test_draw_world_prefers_grid_path_when_available(void) {
    g_tests_run++;
    Renderer renderer = make_renderer(8, 6, 65536);
    proto_world world = make_world(8, 6);

    world.colony_count = 1;
    world.colonies[0].id = 1;
    world.colonies[0].alive = true;
    world.colonies[0].x = 4.0f;
    world.colonies[0].y = 3.0f;
    world.colonies[0].radius = 2.0f;
    world.colonies[0].color_r = 30;
    world.colonies[0].color_g = 60;
    world.colonies[0].color_b = 90;

    world.has_grid = true;
    world.grid_size = world.width * world.height;
    world.grid = (uint16_t*)calloc(world.grid_size, sizeof(uint16_t));
    assert(world.grid != NULL);

    renderer_clear(&renderer);
    renderer_draw_world(&renderer, &world);
    renderer.frame_buffer[renderer.buffer_used] = '\0';

    assert(strstr(renderer.frame_buffer, CELL_COLONY) == NULL);
    assert(strstr(renderer.frame_buffer, CELL_BORDER) == NULL);

    free(world.grid);
    free_renderer(&renderer);
}

static void test_draw_world_falls_back_to_centroid_path(void) {
    g_tests_run++;
    Renderer renderer = make_renderer(8, 6, 65536);
    proto_world world = make_world(8, 6);

    world.colony_count = 1;
    world.colonies[0].id = 1;
    world.colonies[0].alive = true;
    world.colonies[0].x = 4.0f;
    world.colonies[0].y = 3.0f;
    world.colonies[0].radius = 2.0f;
    world.colonies[0].color_r = 30;
    world.colonies[0].color_g = 60;
    world.colonies[0].color_b = 90;
    world.colonies[0].shape_seed = 123;
    world.colonies[0].wobble_phase = 0.0f;
    world.colonies[0].shape_evolution = 0.2f;

    world.has_grid = false;
    world.grid = NULL;
    world.grid_size = 0;

    renderer_clear(&renderer);
    renderer_draw_world(&renderer, &world);
    renderer.frame_buffer[renderer.buffer_used] = '\0';

    assert(strstr(renderer.frame_buffer, CELL_COLONY) != NULL ||
           strstr(renderer.frame_buffer, CELL_BORDER) != NULL);

    free_renderer(&renderer);
}

static void test_draw_world_grid_skips_unknown_and_dead_colonies(void) {
    g_tests_run++;
    Renderer renderer = make_renderer(4, 4, 65536);
    proto_world world = make_world(4, 4);
    world.has_grid = true;
    world.grid_size = world.width * world.height;
    world.grid = (uint16_t*)calloc(world.grid_size, sizeof(uint16_t));
    assert(world.grid != NULL);

    world.colony_count = 1;
    world.colonies[0].id = 7;
    world.colonies[0].alive = false;
    world.colonies[0].color_r = 10;
    world.colonies[0].color_g = 20;
    world.colonies[0].color_b = 30;

    world.grid[0] = 7;
    world.grid[1] = 99;

    renderer_clear(&renderer);
    renderer_draw_world_grid(&renderer, &world);
    renderer.frame_buffer[renderer.buffer_used] = '\0';

    assert(strstr(renderer.frame_buffer, CELL_COLONY) == NULL);
    assert(strstr(renderer.frame_buffer, CELL_BORDER) == NULL);

    free(world.grid);
    free_renderer(&renderer);
}

static void test_draw_world_grid_border_detection_and_highlight(void) {
    g_tests_run++;
    Renderer renderer = make_renderer(3, 3, 65536);
    proto_world world = make_world(3, 3);
    world.has_grid = true;
    world.grid_size = world.width * world.height;
    world.grid = (uint16_t*)calloc(world.grid_size, sizeof(uint16_t));
    assert(world.grid != NULL);

    world.colony_count = 1;
    world.colonies[0].id = 5;
    world.colonies[0].alive = true;
    world.colonies[0].color_r = 30;
    world.colonies[0].color_g = 60;
    world.colonies[0].color_b = 90;

    for (uint32_t i = 0; i < world.grid_size; i++) {
        world.grid[i] = 5;
    }

    renderer.selected_colony = 5;
    renderer_clear(&renderer);
    renderer_draw_world_grid(&renderer, &world);
    renderer.frame_buffer[renderer.buffer_used] = '\0';

    int border_count = count_substring(renderer.frame_buffer, CELL_BORDER);
    int colony_count = count_substring(renderer.frame_buffer, CELL_COLONY);
    (void)border_count;
    (void)colony_count;

    assert(border_count > 0);
    assert(colony_count > 0);
    assert(strstr(renderer.frame_buffer, "38;2;105;125;145m") != NULL);

    free(world.grid);
    free_renderer(&renderer);
}

static void test_scroll_and_center_clamp_to_zero(void) {
    g_tests_run++;
    Renderer renderer = make_renderer(10, 8, 128);

    renderer.view_x = 1;
    renderer.view_y = 1;
    renderer_scroll(&renderer, -10, -20);
    assert(renderer.view_x == 0);
    assert(renderer.view_y == 0);

    renderer_center_on(&renderer, 1, 2);
    assert(renderer.view_x == 0);
    assert(renderer.view_y == 0);

    free_renderer(&renderer);
}

int main(void) {
    test_draw_world_prefers_grid_path_when_available();
    test_draw_world_falls_back_to_centroid_path();
    test_draw_world_grid_skips_unknown_and_dead_colonies();
    test_draw_world_grid_border_detection_and_highlight();
    test_scroll_and_center_clamp_to_zero();

    printf("test_renderer_logic_surface: %d tests passed\n", g_tests_run);
    return 0;
}
