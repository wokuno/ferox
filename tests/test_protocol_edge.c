/**
 * test_protocol_edge.c - Protocol edge case tests
 * Tests serialization/deserialization edge cases and malformed input handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <arpa/inet.h>

#include "../src/shared/protocol.h"

// Test framework
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s\n    At %s:%d\n", msg, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) ASSERT(cond, #cond)
#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_NE(a, b) ASSERT((a) != (b), #a " != " #b)
#define ASSERT_GE(a, b) ASSERT((a) >= (b), #a " >= " #b)
#define ASSERT_LE(a, b) ASSERT((a) <= (b), #a " <= " #b)
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL, #ptr " is not NULL")
#define ASSERT_FALSE(cond) ASSERT(!(cond), "Expected condition to be false: " #cond)

static void write_u32_be(uint8_t* buffer, uint32_t value) {
    uint32_t net = htonl(value);
    memcpy(buffer, &net, sizeof(net));
}

// ============================================================================
// Empty World Tests
// ============================================================================

TEST(empty_world_serialization) {
    proto_world world;
    memset(&world, 0, sizeof(proto_world));
    world.width = 100;
    world.height = 100;
    world.tick = 0;
    world.colony_count = 0;
    world.paused = false;
    world.speed_multiplier = 1.0f;
    
    uint8_t* buffer = NULL;
    size_t len = 0;
    
    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(buffer);
    ASSERT(len > 0, "Buffer length should be > 0");
    
    // Deserialize and verify
    proto_world deserialized;
    result = protocol_deserialize_world_state(buffer, len, &deserialized);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(deserialized.width, 100);
    ASSERT_EQ(deserialized.height, 100);
    ASSERT_EQ(deserialized.colony_count, 0);
    
    free(buffer);
}

// ============================================================================
// Many Colonies Tests
// ============================================================================

TEST(world_with_max_colonies) {
    proto_world world;
    memset(&world, 0, sizeof(proto_world));
    world.width = 200;
    world.height = 200;
    world.tick = 12345;
    world.colony_count = MAX_COLONIES;
    world.paused = true;
    world.speed_multiplier = 2.5f;
    
    // Initialize all colonies
    for (int i = 0; i < MAX_COLONIES; i++) {
        world.colonies[i].id = i + 1;
        snprintf(world.colonies[i].name, MAX_COLONY_NAME, "Colony%d", i);
        world.colonies[i].x = (float)(i % 200);
        world.colonies[i].y = (float)(i / 200);
        world.colonies[i].radius = 5.0f;
        world.colonies[i].population = i * 10;
        world.colonies[i].growth_rate = 0.5f;
        world.colonies[i].color_r = (uint8_t)(i % 256);
        world.colonies[i].color_g = (uint8_t)((i * 2) % 256);
        world.colonies[i].color_b = (uint8_t)((i * 3) % 256);
        world.colonies[i].alive = true;
    }
    
    uint8_t* buffer = NULL;
    size_t len = 0;
    
    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(buffer);
    
    // Deserialize
    proto_world deserialized;
    result = protocol_deserialize_world_state(buffer, len, &deserialized);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(deserialized.colony_count, MAX_COLONIES);
    
    // Verify some colonies
    ASSERT_EQ(deserialized.colonies[0].id, 1);
    ASSERT_EQ(deserialized.colonies[MAX_COLONIES - 1].id, MAX_COLONIES);
    
    free(buffer);
}

TEST(world_with_1000_colonies_capped) {
    proto_world world;
    memset(&world, 0, sizeof(proto_world));
    world.colony_count = MAX_COLONIES;  // Cap at max
    
    for (uint32_t i = 0; i < MAX_COLONIES; i++) {
        world.colonies[i].id = i + 1;
        world.colonies[i].population = i;
    }
    
    uint8_t* buffer = NULL;
    size_t len = 0;
    
    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    
    proto_world deserialized;
    result = protocol_deserialize_world_state(buffer, len, &deserialized);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(deserialized.colony_count, MAX_COLONIES);
    
    free(buffer);
}

// ============================================================================
// Header Tests
// ============================================================================

TEST(header_serialization_roundtrip) {
    MessageHeader header = {
        .magic = PROTOCOL_MAGIC,
        .type = MSG_WORLD_STATE,
        .payload_len = 12345,
        .sequence = 42
    };
    
    uint8_t buffer[MESSAGE_HEADER_SIZE];
    
    int result = protocol_serialize_header(&header, buffer);
    ASSERT_EQ(result, MESSAGE_HEADER_SIZE);
    
    MessageHeader deserialized;
    result = protocol_deserialize_header(buffer, &deserialized);
    ASSERT_EQ(result, MESSAGE_HEADER_SIZE);
    ASSERT_EQ(deserialized.magic, PROTOCOL_MAGIC);
    ASSERT_EQ(deserialized.type, MSG_WORLD_STATE);
    ASSERT_EQ(deserialized.payload_len, 12345);
    ASSERT_EQ(deserialized.sequence, 42);
}

TEST(malformed_header_wrong_magic) {
    uint8_t buffer[MESSAGE_HEADER_SIZE];
    
    // Write wrong magic number
    buffer[0] = 0xDE;
    buffer[1] = 0xAD;
    buffer[2] = 0xBE;
    buffer[3] = 0xEF;
    
    MessageHeader header;
    int result = protocol_deserialize_header(buffer, &header);
    ASSERT_EQ(result, -1);  // Should fail
}

TEST(header_all_message_types) {
    MessageType types[] = {
        MSG_CONNECT, MSG_DISCONNECT, MSG_WORLD_STATE, MSG_WORLD_DELTA,
        MSG_COLONY_INFO, MSG_COMMAND, MSG_ACK, MSG_ERROR
    };
    
    for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        MessageHeader header = {
            .magic = PROTOCOL_MAGIC,
            .type = types[i],
            .payload_len = 100,
            .sequence = (uint32_t)i
        };
        
        uint8_t buffer[MESSAGE_HEADER_SIZE];
        protocol_serialize_header(&header, buffer);
        
        MessageHeader deserialized;
        int result = protocol_deserialize_header(buffer, &deserialized);
        ASSERT_EQ(result, MESSAGE_HEADER_SIZE);
        ASSERT_EQ(deserialized.type, types[i]);
    }
}

// ============================================================================
// Payload Tests
// ============================================================================

TEST(zero_length_payload) {
    proto_world world;
    memset(&world, 0, sizeof(proto_world));
    world.width = 10;
    world.height = 10;
    world.colony_count = 0;
    
    uint8_t* buffer = NULL;
    size_t len = 0;
    
    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    
    // Should still have header data
    ASSERT(len > 0, "Empty world should still serialize");
    
    free(buffer);
}

TEST(partial_message_handling) {
    proto_world world;
    memset(&world, 0, sizeof(proto_world));
    world.width = 50;
    world.height = 50;
    world.colony_count = 5;
    
    for (int i = 0; i < 5; i++) {
        world.colonies[i].id = i + 1;
        snprintf(world.colonies[i].name, MAX_COLONY_NAME, "Test%d", i);
    }
    
    uint8_t* buffer = NULL;
    size_t len = 0;
    
    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    
    // Try to deserialize with truncated buffer
    proto_world partial;
    result = protocol_deserialize_world_state(buffer, 10, &partial);  // Too short
    ASSERT_EQ(result, -1);  // Should fail
    
    free(buffer);
}

TEST(rejects_world_state_with_too_many_colonies) {
    proto_world world;
    memset(&world, 0, sizeof(proto_world));
    world.width = 8;
    world.height = 8;

    uint8_t* buffer = NULL;
    size_t len = 0;

    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    ASSERT_GE(len, 26);

    write_u32_be(buffer + 12, MAX_COLONIES + 1);

    proto_world deserialized;
    memset(&deserialized, 0, sizeof(proto_world));
    result = protocol_deserialize_world_state(buffer, len, &deserialized);
    ASSERT_EQ(result, -1);

    free(buffer);
}

TEST(grid_metadata_out_of_bounds_disables_grid) {
    proto_world world;
    memset(&world, 0, sizeof(proto_world));
    world.width = 5;
    world.height = 5;

    uint8_t* buffer = NULL;
    size_t len = 0;

    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    ASSERT_GE(len, 26);

    buffer[21] = 1;
    write_u32_be(buffer + 22, 10);

    proto_world deserialized;
    memset(&deserialized, 0, sizeof(proto_world));
    result = protocol_deserialize_world_state(buffer, len, &deserialized);
    ASSERT_EQ(result, 0);
    ASSERT_FALSE(deserialized.has_grid);
    ASSERT_EQ(deserialized.grid, NULL);
    ASSERT_EQ(deserialized.grid_size, 0);

    free(buffer);
}

TEST(invalid_rle_payload_disables_grid) {
    proto_world world;
    memset(&world, 0, sizeof(proto_world));
    world.width = 2;
    world.height = 2;
    world.has_grid = true;
    world.grid_size = 4;

    uint16_t grid_data[4] = {1, 1, 1, 1};
    world.grid = grid_data;

    uint8_t* buffer = NULL;
    size_t len = 0;

    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    ASSERT(buffer[21] == 1, "Serialized world should include grid data");

    write_u32_be(buffer + 26, 9999);

    proto_world deserialized;
    memset(&deserialized, 0, sizeof(proto_world));
    result = protocol_deserialize_world_state(buffer, len, &deserialized);
    ASSERT_EQ(result, 0);
    ASSERT_FALSE(deserialized.has_grid);

    proto_world_free(&deserialized);
    free(buffer);
}

// ============================================================================
// Colony Name Tests
// ============================================================================

TEST(maximum_length_colony_name) {
    proto_colony colony;
    memset(&colony, 0, sizeof(proto_colony));
    colony.id = 1;
    
    // Fill name to max length
    for (int i = 0; i < MAX_COLONY_NAME - 1; i++) {
        colony.name[i] = 'A' + (i % 26);
    }
    colony.name[MAX_COLONY_NAME - 1] = '\0';
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    proto_colony deserialized;
    size = protocol_deserialize_colony(buffer, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    
    ASSERT_EQ(strlen(deserialized.name), MAX_COLONY_NAME - 1);
}

TEST(special_characters_in_names) {
    proto_colony colony;
    memset(&colony, 0, sizeof(proto_colony));
    colony.id = 42;
    strncpy(colony.name, "Test!@#$%^&*()_+-=[]", MAX_COLONY_NAME - 1);
    colony.name[MAX_COLONY_NAME - 1] = '\0';
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    proto_colony deserialized;
    size = protocol_deserialize_colony(buffer, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    
    ASSERT_EQ(strcmp(colony.name, deserialized.name), 0);
}

TEST(unicode_like_bytes_in_names) {
    proto_colony colony;
    memset(&colony, 0, sizeof(proto_colony));
    colony.id = 1;
    
    // High byte values (would be UTF-8 multi-byte chars)
    colony.name[0] = (char)0xC3;
    colony.name[1] = (char)0xA9;  // é in UTF-8
    colony.name[2] = (char)0x00;
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    proto_colony deserialized;
    size = protocol_deserialize_colony(buffer, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
}

TEST(empty_colony_name) {
    proto_colony colony;
    memset(&colony, 0, sizeof(proto_colony));
    colony.id = 1;
    colony.name[0] = '\0';  // Empty name
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    proto_colony deserialized;
    size = protocol_deserialize_colony(buffer, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    ASSERT_EQ(strlen(deserialized.name), 0);
}

// ============================================================================
// Command Tests
// ============================================================================

TEST(all_command_types) {
    CommandType commands[] = {
        CMD_PAUSE, CMD_RESUME, CMD_SPEED_UP, CMD_SLOW_DOWN, CMD_RESET
    };
    
    uint8_t buffer[64];
    
    for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); i++) {
        int size = protocol_serialize_command(commands[i], NULL, buffer);
        ASSERT(size > 0, "Command serialization should succeed");
        
        CommandType deserialized;
        size = protocol_deserialize_command(buffer, (size_t)size, &deserialized, NULL);
        ASSERT(size > 0, "Command deserialization should succeed");
        ASSERT_EQ(deserialized, commands[i]);
    }
}

TEST(select_colony_command) {
    CommandSelectColony data = { .colony_id = 12345 };
    uint8_t buffer[64];
    
    int size = protocol_serialize_command(CMD_SELECT_COLONY, &data, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    CommandType cmd;
    CommandSelectColony deserialized;
    size = protocol_deserialize_command(buffer, (size_t)size, &cmd, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    ASSERT_EQ(cmd, CMD_SELECT_COLONY);
    ASSERT_EQ(deserialized.colony_id, 12345);
}

TEST(spawn_colony_command) {
    CommandSpawnColony data = {
        .x = 123.456f,
        .y = 789.012f
    };
    strncpy(data.name, "NewColony", MAX_COLONY_NAME);
    
    uint8_t buffer[128];
    
    int size = protocol_serialize_command(CMD_SPAWN_COLONY, &data, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    CommandType cmd;
    CommandSpawnColony deserialized;
    size = protocol_deserialize_command(buffer, (size_t)size, &cmd, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    ASSERT_EQ(cmd, CMD_SPAWN_COLONY);
    
    // Float comparison with tolerance
    ASSERT(fabsf(deserialized.x - 123.456f) < 0.001f, "X should match");
    ASSERT(fabsf(deserialized.y - 789.012f) < 0.001f, "Y should match");
}

TEST(rejects_truncated_command_payload) {
    uint8_t buffer[128];
    CommandType cmd;

    CommandSelectColony select_data = { .colony_id = 123 };
    int select_size = protocol_serialize_command(CMD_SELECT_COLONY, &select_data, buffer);
    ASSERT(select_size > 0, "Serialization should succeed");
    ASSERT_EQ(protocol_deserialize_command(buffer, 4, &cmd, NULL), -1);

    CommandSpawnColony spawn_data = { .x = 1.0f, .y = 2.0f };
    strncpy(spawn_data.name, "tiny", MAX_COLONY_NAME);
    int spawn_size = protocol_serialize_command(CMD_SPAWN_COLONY, &spawn_data, buffer);
    ASSERT(spawn_size > 0, "Serialization should succeed");
    ASSERT_EQ(protocol_deserialize_command(buffer, 8, &cmd, NULL), -1);
}

TEST(rejects_unknown_command_type) {
    uint8_t buffer[4];
    CommandType cmd;

    write_u32_be(buffer, 999);
    ASSERT_EQ(protocol_deserialize_command(buffer, sizeof(buffer), &cmd, NULL), -1);
}

// ============================================================================
// Null Input Tests
// ============================================================================

TEST(null_inputs_handled) {
    uint8_t buffer[64];
    
    // Header null checks
    ASSERT_EQ(protocol_serialize_header(NULL, buffer), -1);
    ASSERT_EQ(protocol_serialize_header(NULL, NULL), -1);
    
    MessageHeader header = { .magic = PROTOCOL_MAGIC };
    ASSERT_EQ(protocol_serialize_header(&header, NULL), -1);
    ASSERT_EQ(protocol_deserialize_header(NULL, &header), -1);
    ASSERT_EQ(protocol_deserialize_header(buffer, NULL), -1);
    
    // Colony null checks
    ASSERT_EQ(protocol_serialize_colony(NULL, buffer), -1);
    ASSERT_EQ(protocol_serialize_colony(NULL, NULL), -1);
    
    proto_colony colony;
    ASSERT_EQ(protocol_serialize_colony(&colony, NULL), -1);
    ASSERT_EQ(protocol_deserialize_colony(NULL, &colony), -1);
    ASSERT_EQ(protocol_deserialize_colony(buffer, NULL), -1);
    
    // World null checks
    uint8_t* buf_ptr = NULL;
    size_t len;
    ASSERT_EQ(protocol_serialize_world_state(NULL, &buf_ptr, &len), -1);
    
    proto_world world;
    ASSERT_EQ(protocol_serialize_world_state(&world, NULL, &len), -1);
    ASSERT_EQ(protocol_serialize_world_state(&world, &buf_ptr, NULL), -1);
    
    ASSERT_EQ(protocol_deserialize_world_state(NULL, 100, &world), -1);
    ASSERT_EQ(protocol_deserialize_world_state(buffer, 100, NULL), -1);
}

// ============================================================================
// Boundary Values
// ============================================================================

TEST(extreme_float_values) {
    proto_colony colony;
    memset(&colony, 0, sizeof(proto_colony));
    colony.id = 1;
    strcpy(colony.name, "Test");
    colony.x = 999999.999f;
    colony.y = -999999.999f;
    colony.radius = 0.0f;
    colony.growth_rate = 1.0f;
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    proto_colony deserialized;
    size = protocol_deserialize_colony(buffer, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    
    ASSERT(fabsf(deserialized.x - colony.x) < 0.01f, "X should match");
    ASSERT(fabsf(deserialized.y - colony.y) < 0.01f, "Y should match");
}

TEST(max_population_value) {
    proto_colony colony;
    memset(&colony, 0, sizeof(proto_colony));
    colony.id = 1;
    colony.population = 0xFFFFFFFF;  // Max uint32
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    protocol_serialize_colony(&colony, buffer);
    
    proto_colony deserialized;
    protocol_deserialize_colony(buffer, &deserialized);
    
    ASSERT_EQ(deserialized.population, 0xFFFFFFFF);
}

// ============================================================================
// Run Tests
// ============================================================================

int run_protocol_edge_tests(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    printf("\n=== Protocol Edge Case Tests ===\n\n");
    
    printf("Empty World Tests:\n");
    RUN_TEST(empty_world_serialization);
    
    printf("\nMany Colonies Tests:\n");
    RUN_TEST(world_with_max_colonies);
    RUN_TEST(world_with_1000_colonies_capped);
    
    printf("\nHeader Tests:\n");
    RUN_TEST(header_serialization_roundtrip);
    RUN_TEST(malformed_header_wrong_magic);
    RUN_TEST(header_all_message_types);
    
    printf("\nPayload Tests:\n");
    RUN_TEST(zero_length_payload);
    RUN_TEST(partial_message_handling);
    RUN_TEST(rejects_world_state_with_too_many_colonies);
    RUN_TEST(grid_metadata_out_of_bounds_disables_grid);
    RUN_TEST(invalid_rle_payload_disables_grid);
    
    printf("\nColony Name Tests:\n");
    RUN_TEST(maximum_length_colony_name);
    RUN_TEST(special_characters_in_names);
    RUN_TEST(unicode_like_bytes_in_names);
    RUN_TEST(empty_colony_name);
    
    printf("\nCommand Tests:\n");
    RUN_TEST(all_command_types);
    RUN_TEST(select_colony_command);
    RUN_TEST(spawn_colony_command);
    RUN_TEST(rejects_truncated_command_payload);
    RUN_TEST(rejects_unknown_command_type);
    
    printf("\nNull Input Tests:\n");
    RUN_TEST(null_inputs_handled);
    
    printf("\nBoundary Values:\n");
    RUN_TEST(extreme_float_values);
    RUN_TEST(max_population_value);
    
    printf("\n--- Protocol Edge Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_protocol_edge_tests() > 0 ? 1 : 0;
}
#endif
