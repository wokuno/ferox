#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "../src/client/renderer.h"
#include "../src/client/input.h"
#include "../src/client/client.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing %s... ", #name); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASSED\n"); \
        tests_passed++; \
    } else { \
        printf("FAILED\n"); \
    } \
} while(0)

// Test ANSI color formatting
static int test_ansi_format_produces_valid_escape_codes(void) {
    char buf[64];
    
    // Test foreground color
    int len = format_ansi_rgb_fg(buf, sizeof(buf), 255, 128, 64);
    if (len <= 0) return 0;
    if (strcmp(buf, "\033[38;2;255;128;64m") != 0) return 0;
    
    // Test background color
    len = format_ansi_rgb_bg(buf, sizeof(buf), 0, 255, 0);
    if (len <= 0) return 0;
    if (strcmp(buf, "\033[48;2;0;255;0m") != 0) return 0;
    
    // Test black
    len = format_ansi_rgb_fg(buf, sizeof(buf), 0, 0, 0);
    if (len <= 0) return 0;
    if (strcmp(buf, "\033[38;2;0;0;0m") != 0) return 0;
    
    // Test white
    len = format_ansi_rgb_bg(buf, sizeof(buf), 255, 255, 255);
    if (len <= 0) return 0;
    if (strcmp(buf, "\033[48;2;255;255;255m") != 0) return 0;
    
    return 1;
}

// Test renderer creation
static int test_renderer_creates_with_valid_buffer(void) {
    Renderer* r = renderer_create();
    if (!r) return 0;
    
    // Check reasonable defaults
    if (r->term_width <= 0 || r->term_height <= 0) {
        renderer_destroy(r);
        return 0;
    }
    
    if (r->frame_buffer == NULL) {
        renderer_destroy(r);
        return 0;
    }
    
    if (r->buffer_size == 0) {
        renderer_destroy(r);
        return 0;
    }
    
    renderer_destroy(r);
    return 1;
}

// Test renderer scroll
static int test_renderer_scroll_moves_view_position(void) {
    Renderer* r = renderer_create();
    if (!r) return 0;
    
    int initial_x = r->view_x;
    int initial_y = r->view_y;
    
    // Scroll right and down
    renderer_scroll(r, 10, 5);
    if (r->view_x != initial_x + 10) {
        renderer_destroy(r);
        return 0;
    }
    if (r->view_y != initial_y + 5) {
        renderer_destroy(r);
        return 0;
    }
    
    // Try to scroll past origin (should clamp)
    renderer_scroll(r, -100, -100);
    if (r->view_x < 0 || r->view_y < 0) {
        renderer_destroy(r);
        return 0;
    }
    
    renderer_destroy(r);
    return 1;
}

// Test renderer center on
static int test_renderer_center_on_sets_view_position(void) {
    Renderer* r = renderer_create();
    if (!r) return 0;
    
    // Center on a point
    renderer_center_on(r, 100, 50);
    
    // The view should be centered on (100, 50)
    // view_x should be 100 - view_width/2
    int expected_x = 100 - r->view_width / 2;
    int expected_y = 50 - r->view_height / 2;
    
    // Clamp expected values
    if (expected_x < 0) expected_x = 0;
    if (expected_y < 0) expected_y = 0;
    
    if (r->view_x != expected_x || r->view_y != expected_y) {
        renderer_destroy(r);
        return 0;
    }
    
    renderer_destroy(r);
    return 1;
}

// Test renderer write functions
static int test_renderer_write_adds_text_to_buffer(void) {
    Renderer* r = renderer_create();
    if (!r) return 0;
    
    renderer_clear(r);
    
    // Write some text
    renderer_write(r, "Hello");
    if (r->buffer_used < 5) {
        renderer_destroy(r);
        return 0;
    }
    
    renderer_writef(r, " World %d", 42);
    if (r->buffer_used < 14) {
        renderer_destroy(r);
        return 0;
    }
    
    renderer_destroy(r);
    return 1;
}

// Test client creation
static int test_client_creates_with_default_state(void) {
    Client* c = client_create();
    if (!c) return 0;
    
    // Check initial state
    if (c->connected != false) {
        client_destroy(c);
        return 0;
    }
    
    if (c->running != false) {
        client_destroy(c);
        return 0;
    }
    
    if (c->renderer == NULL) {
        client_destroy(c);
        return 0;
    }
    
    if (c->selected_colony != 0) {
        client_destroy(c);
        return 0;
    }
    
    client_destroy(c);
    return 1;
}

// Test client colony selection
static int test_client_selects_next_alive_colony(void) {
    Client* c = client_create();
    if (!c) return 0;
    
    // Set up test world with colonies
    c->local_world.colony_count = 3;
    c->local_world.colonies[0] = (ProtoColony){1, "Alpha", 10.0f, 10.0f, 5.0f, 50, 50, 0.5f, 255, 0, 0, true, 0, 0.0f};
    c->local_world.colonies[1] = (ProtoColony){2, "Beta", 20.0f, 20.0f, 5.0f, 50, 50, 0.5f, 0, 255, 0, false, 0, 0.0f}; // dead
    c->local_world.colonies[2] = (ProtoColony){3, "Gamma", 30.0f, 30.0f, 5.0f, 50, 50, 0.5f, 0, 0, 255, true, 0, 0.0f};
    
    // Initially no selection
    if (c->selected_colony != 0) {
        client_destroy(c);
        return 0;
    }
    
    // Select next - should get first alive colony
    client_select_next_colony(c);
    if (c->selected_colony != 1) {
        client_destroy(c);
        return 0;
    }
    
    // Select next - should skip dead colony and get third
    client_select_next_colony(c);
    if (c->selected_colony != 3) {
        client_destroy(c);
        return 0;
    }
    
    // Deselect
    client_deselect_colony(c);
    if (c->selected_colony != 0) {
        client_destroy(c);
        return 0;
    }
    
    client_destroy(c);
    return 1;
}

// Test get selected colony
static int test_client_get_selected_returns_correct_colony(void) {
    Client* c = client_create();
    if (!c) return 0;
    
    // Set up test world
    c->local_world.colony_count = 2;
    c->local_world.colonies[0] = (ProtoColony){1, "Alpha", 10.0f, 10.0f, 5.0f, 50, 50, 0.5f, 255, 0, 0, true, 0, 0.0f};
    c->local_world.colonies[1] = (ProtoColony){2, "Beta", 20.0f, 20.0f, 5.0f, 50, 50, 0.5f, 0, 255, 0, true, 0, 0.0f};
    
    // No selection initially
    const ProtoColony* sel = client_get_selected_colony(c);
    if (sel != NULL) {
        client_destroy(c);
        return 0;
    }
    
    // Select first
    c->selected_colony = 1;
    sel = client_get_selected_colony(c);
    if (sel == NULL || sel->id != 1) {
        client_destroy(c);
        return 0;
    }
    
    // Select second
    c->selected_colony = 2;
    sel = client_get_selected_colony(c);
    if (sel == NULL || sel->id != 2) {
        client_destroy(c);
        return 0;
    }
    
    // Invalid selection
    c->selected_colony = 999;
    sel = client_get_selected_colony(c);
    if (sel != NULL) {
        client_destroy(c);
        return 0;
    }
    
    client_destroy(c);
    return 1;
}

// Test input initialization status
static int test_input_is_not_initialized_by_default(void) {
    // Initially should not be initialized
    if (input_is_initialized()) return 0;
    
    // Don't actually init in test (would mess with terminal)
    // Just verify the API exists and returns expected values
    
    return 1;
}

// Test color output produces valid escape sequences
static int test_color_escape_sequences_are_valid(void) {
    char buf[64];
    
    // Test various colors
    struct { uint8_t r, g, b; } test_colors[] = {
        {0, 0, 0},       // Black
        {255, 255, 255}, // White
        {255, 0, 0},     // Red
        {0, 255, 0},     // Green
        {0, 0, 255},     // Blue
        {128, 128, 128}, // Gray
        {255, 128, 64},  // Orange-ish
    };
    
    for (size_t i = 0; i < sizeof(test_colors) / sizeof(test_colors[0]); i++) {
        int len = format_ansi_rgb_fg(buf, sizeof(buf), 
                                      test_colors[i].r, 
                                      test_colors[i].g, 
                                      test_colors[i].b);
        if (len <= 0) return 0;
        
        // Should start with escape sequence
        if (buf[0] != '\033' || buf[1] != '[') return 0;
        
        // Should end with 'm'
        if (buf[len-1] != 'm') return 0;
    }
    
    return 1;
}

// Test world update
static int test_client_world_state_can_be_updated(void) {
    Client* c = client_create();
    if (!c) return 0;
    
    // Initial world state
    if (c->local_world.tick != 0) {
        client_destroy(c);
        return 0;
    }
    
    if (c->local_world.colony_count != 0) {
        client_destroy(c);
        return 0;
    }
    
    // Manually set values (simulating update)
    c->local_world.tick = 100;
    c->local_world.colony_count = 5;
    c->local_world.paused = true;
    
    if (c->local_world.tick != 100) {
        client_destroy(c);
        return 0;
    }
    
    if (!c->local_world.paused) {
        client_destroy(c);
        return 0;
    }
    
    client_destroy(c);
    return 1;
}

static int test_client_handles_colony_detail_messages(void) {
    Client* c = client_create();
    if (!c) return 0;

    c->selected_colony = 7;

    ProtoColonyDetail detail;
    memset(&detail, 0, sizeof(detail));
    detail.base.id = 7;
    strcpy(detail.base.name, "Alpha");
    detail.base.alive = true;
    detail.state = 2;
    detail.flags = COLONY_DETAIL_FLAG_DORMANT;
    detail.behavior_mode = PROTO_COLONY_BEHAVIOR_MODE_FORTIFYING;
    detail.focus_direction = 4;
    detail.dominant_sensor = PROTO_COLONY_SENSOR_ALARM;
    detail.dominant_drive = PROTO_COLONY_DRIVE_CAUTION;
    detail.secondary_sensor = PROTO_COLONY_SENSOR_PRESSURE;
    detail.secondary_drive = PROTO_COLONY_DRIVE_PRESERVATION;
    detail.sensor_link_sensor = PROTO_COLONY_SENSOR_ALARM;
    detail.sensor_link_drive = PROTO_COLONY_DRIVE_CAUTION;
    detail.action_link_drive = PROTO_COLONY_DRIVE_CAUTION;
    detail.action_link_action = PROTO_COLONY_ACTION_DEFEND;
    detail.secondary_sensor_link_sensor = PROTO_COLONY_SENSOR_PRESSURE;
    detail.secondary_sensor_link_drive = PROTO_COLONY_DRIVE_PRESERVATION;
    detail.secondary_action_link_drive = PROTO_COLONY_DRIVE_PRESERVATION;
    detail.secondary_action_link_action = PROTO_COLONY_ACTION_DORMANCY;
    detail.dominant_sensor_value = 0.88f;
    detail.dominant_drive_value = 0.73f;
    detail.secondary_sensor_value = 0.64f;
    detail.secondary_drive_value = 0.61f;
    detail.sensor_link_value = 0.69f;
    detail.action_link_value = 0.74f;
    detail.secondary_sensor_link_value = 0.51f;
    detail.secondary_action_link_value = 0.57f;
    detail.action_defend = 0.7f;
    detail.action_transfer = 0.4f;
    detail.action_dormancy = 0.6f;
    detail.action_motility = 0.2f;
    detail.stress_level = 0.8f;
    detail.trait_learning = 0.6f;

    uint8_t buffer[COLONY_DETAIL_SERIALIZED_SIZE];
    int len = protocol_serialize_colony_detail(&detail, buffer);
    if (len != COLONY_DETAIL_SERIALIZED_SIZE) {
        client_destroy(c);
        return 0;
    }

    client_handle_message(c, MSG_COLONY_INFO, buffer, (size_t)len);
    if (!c->has_selected_detail) {
        client_destroy(c);
        return 0;
    }
    if (c->selected_detail.base.id != 7 || c->selected_detail.state != 2) {
        client_destroy(c);
        return 0;
    }
    if (c->selected_detail.behavior_mode != PROTO_COLONY_BEHAVIOR_MODE_FORTIFYING) {
        client_destroy(c);
        return 0;
    }
    if (c->selected_detail.dominant_sensor != PROTO_COLONY_SENSOR_ALARM ||
        c->selected_detail.dominant_drive != PROTO_COLONY_DRIVE_CAUTION) {
        client_destroy(c);
        return 0;
    }
    if (c->selected_detail.secondary_sensor != PROTO_COLONY_SENSOR_PRESSURE ||
        c->selected_detail.secondary_drive != PROTO_COLONY_DRIVE_PRESERVATION) {
        client_destroy(c);
        return 0;
    }
    if (c->selected_detail.action_link_action != PROTO_COLONY_ACTION_DEFEND ||
        c->selected_detail.sensor_link_drive != PROTO_COLONY_DRIVE_CAUTION) {
        client_destroy(c);
        return 0;
    }
    if (c->selected_detail.secondary_action_link_action != PROTO_COLONY_ACTION_DORMANCY ||
        c->selected_detail.secondary_sensor_link_drive != PROTO_COLONY_DRIVE_PRESERVATION) {
        client_destroy(c);
        return 0;
    }
    if (fabsf(c->selected_detail.action_dormancy - 0.6f) > 0.0001f) {
        client_destroy(c);
        return 0;
    }
    if ((c->selected_detail.flags & COLONY_DETAIL_FLAG_DORMANT) == 0) {
        client_destroy(c);
        return 0;
    }

    client_destroy(c);
    return 1;
}

static int test_client_handles_command_status_messages(void) {
    Client* c = client_create();
    if (!c) {
        return 0;
    }

    ProtoCommandStatus status;
    memset(&status, 0, sizeof(status));
    c->selected_colony = 99;
    status.command = CMD_SELECT_COLONY;
    status.status_code = PROTO_COMMAND_STATUS_ACCEPTED;
    status.entity_id = 11;
    strcpy(status.message, "Selection accepted");

    uint8_t buffer[COMMAND_STATUS_SERIALIZED_SIZE];
    int len = protocol_serialize_command_status(&status, buffer);
    if (len != COMMAND_STATUS_SERIALIZED_SIZE) {
        client_destroy(c);
        return 0;
    }

    client_handle_message(c, MSG_ACK, buffer, (size_t)len);
    if (!c->has_command_status) {
        client_destroy(c);
        return 0;
    }
    if (c->last_command_status.command != CMD_SELECT_COLONY ||
        c->last_command_status.status_code != PROTO_COMMAND_STATUS_ACCEPTED ||
        strcmp(c->last_command_status.message, "Selection accepted") != 0 ||
        c->selected_colony != 11) {
        client_destroy(c);
        return 0;
    }

    status.status_code = PROTO_COMMAND_STATUS_REJECTED;
    status.entity_id = 77;
    strcpy(status.message, "Selection rejected: colony not found");
    len = protocol_serialize_command_status(&status, buffer);
    if (len != COMMAND_STATUS_SERIALIZED_SIZE) {
        client_destroy(c);
        return 0;
    }

    client_handle_message(c, MSG_ERROR, buffer, (size_t)len);
    if (!c->has_command_status ||
        c->last_command_status.status_code != PROTO_COMMAND_STATUS_REJECTED ||
        strcmp(c->last_command_status.message, "Selection rejected: colony not found") != 0 ||
        c->selected_colony != 0) {
        client_destroy(c);
        return 0;
    }

    client_destroy(c);
    return 1;
}

int main(void) {
    printf("Phase 6 Tests: Client Implementation\n");
    printf("=====================================\n\n");
    
    printf("Renderer Tests:\n");
    TEST(renderer_creates_with_valid_buffer);
    TEST(renderer_scroll_moves_view_position);
    TEST(renderer_center_on_sets_view_position);
    TEST(renderer_write_adds_text_to_buffer);
    
    printf("\nColor Tests:\n");
    TEST(ansi_format_produces_valid_escape_codes);
    TEST(color_escape_sequences_are_valid);
    
    printf("\nClient Tests:\n");
    TEST(client_creates_with_default_state);
    TEST(client_selects_next_alive_colony);
    TEST(client_get_selected_returns_correct_colony);
    TEST(client_world_state_can_be_updated);
    TEST(client_handles_colony_detail_messages);
    TEST(client_handles_command_status_messages);
    
    printf("\nInput Tests:\n");
    TEST(input_is_not_initialized_by_default);
    
    printf("\n=====================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    
    return (tests_passed == tests_run) ? 0 : 1;
}
