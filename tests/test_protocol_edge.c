/**
 * test_protocol_edge.c - Protocol edge case tests
 * Tests serialization/deserialization edge cases and malformed input handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

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
#define ASSERT_BYTES_EQ(actual, expected, len, msg) ASSERT(memcmp((actual), (expected), (len)) == 0, msg)

// ============================================================================
// Empty World Tests
// ============================================================================

TEST(empty_world_serialization) {
    ProtoWorld world;
    memset(&world, 0, sizeof(ProtoWorld));
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
    ProtoWorld deserialized;
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
    ProtoWorld world;
    memset(&world, 0, sizeof(ProtoWorld));
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
    ProtoWorld deserialized;
    result = protocol_deserialize_world_state(buffer, len, &deserialized);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(deserialized.colony_count, MAX_COLONIES);
    
    // Verify some colonies
    ASSERT_EQ(deserialized.colonies[0].id, 1);
    ASSERT_EQ(deserialized.colonies[MAX_COLONIES - 1].id, MAX_COLONIES);
    
    free(buffer);
}

TEST(world_with_1000_colonies_capped) {
    ProtoWorld world;
    memset(&world, 0, sizeof(ProtoWorld));
    world.colony_count = MAX_COLONIES;  // Cap at max
    
    for (uint32_t i = 0; i < MAX_COLONIES; i++) {
        world.colonies[i].id = i + 1;
        world.colonies[i].population = i;
    }
    
    uint8_t* buffer = NULL;
    size_t len = 0;
    
    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    
    ProtoWorld deserialized;
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

TEST(header_wire_bytes_match_spec_example) {
    MessageHeader header = {
        .magic = PROTOCOL_MAGIC,
        .type = MSG_COMMAND,
        .payload_len = 8,
        .sequence = 3,
    };

    uint8_t buffer[MESSAGE_HEADER_SIZE];
    uint8_t expected[MESSAGE_HEADER_SIZE] = {
        0x00, 0x00, 0xBA, 0xCF,
        0x00, 0x05,
        0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x00, 0x03,
    };

    int result = protocol_serialize_header(&header, buffer);
    ASSERT_EQ(result, MESSAGE_HEADER_SIZE);
    ASSERT_BYTES_EQ(buffer, expected, sizeof(expected), "Header bytes should match documented wire example");
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
    ProtoWorld world;
    memset(&world, 0, sizeof(ProtoWorld));
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
    ProtoWorld world;
    memset(&world, 0, sizeof(ProtoWorld));
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
    ProtoWorld partial;
    result = protocol_deserialize_world_state(buffer, 10, &partial);  // Too short
    ASSERT_EQ(result, -1);  // Should fail
    
    free(buffer);
}

TEST(world_delta_grid_chunk_roundtrip) {
    ProtoWorldDeltaGridChunk chunk;
    proto_world_delta_grid_chunk_init(&chunk);
    chunk.tick = 77;
    chunk.width = 512;
    chunk.height = 512;
    chunk.total_cells = 512u * 512u;
    chunk.start_index = 65536u;
    chunk.cell_count = 1024u;
    chunk.final_chunk = false;
    chunk.cells = (uint16_t*)malloc((size_t)chunk.cell_count * sizeof(uint16_t));
    ASSERT_NOT_NULL(chunk.cells);

    for (uint32_t i = 0; i < chunk.cell_count; i++) {
        chunk.cells[i] = (uint16_t)((i * 17u) & 0xFFFFu);
    }

    uint8_t* buffer = NULL;
    size_t len = 0;
    int result = protocol_serialize_world_delta_grid_chunk(&chunk, &buffer, &len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(buffer);

    ProtoWorldDeltaGridChunk decoded;
    proto_world_delta_grid_chunk_init(&decoded);
    result = protocol_deserialize_world_delta_grid_chunk(buffer, len, &decoded);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(decoded.tick, chunk.tick);
    ASSERT_EQ(decoded.width, chunk.width);
    ASSERT_EQ(decoded.height, chunk.height);
    ASSERT_EQ(decoded.total_cells, chunk.total_cells);
    ASSERT_EQ(decoded.start_index, chunk.start_index);
    ASSERT_EQ(decoded.cell_count, chunk.cell_count);
    ASSERT_EQ(decoded.final_chunk, chunk.final_chunk);
    for (uint32_t i = 0; i < decoded.cell_count; i++) {
        ASSERT_EQ(decoded.cells[i], chunk.cells[i]);
    }

    free(buffer);
    proto_world_delta_grid_chunk_free(&decoded);
    proto_world_delta_grid_chunk_free(&chunk);
}

TEST(world_delta_grid_chunk_wire_format) {
    uint16_t cells[] = {1u, 256u, 513u};
    ProtoWorldDeltaGridChunk chunk;
    proto_world_delta_grid_chunk_init(&chunk);
    chunk.tick = 0x01020304u;
    chunk.width = 32u;
    chunk.height = 16u;
    chunk.total_cells = 512u;
    chunk.start_index = 10u;
    chunk.cell_count = 3u;
    chunk.final_chunk = true;
    chunk.cells = cells;

    uint8_t* buffer = NULL;
    size_t len = 0;
    int result = protocol_serialize_world_delta_grid_chunk(&chunk, &buffer, &len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(buffer);
    ASSERT_EQ(len, (size_t)32);

    uint8_t expected[] = {
        0x01,
        0x01, 0x02, 0x03, 0x04,
        0x00, 0x00, 0x00, 0x20,
        0x00, 0x00, 0x00, 0x10,
        0x00, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x0A,
        0x00, 0x00, 0x00, 0x03,
        0x01,
        0x00, 0x01,
        0x01, 0x00,
        0x02, 0x01,
    };
    ASSERT_BYTES_EQ(buffer, expected, sizeof(expected), "World-delta chunk bytes should match big-endian wire format");

    ProtoWorldDeltaGridChunk decoded;
    proto_world_delta_grid_chunk_init(&decoded);
    result = protocol_deserialize_world_delta_grid_chunk(buffer, len, &decoded);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(decoded.final_chunk, true);
    ASSERT_EQ(decoded.cells[0], 1u);
    ASSERT_EQ(decoded.cells[1], 256u);
    ASSERT_EQ(decoded.cells[2], 513u);
    proto_world_delta_grid_chunk_free(&decoded);

    free(buffer);
}

TEST(world_delta_grid_chunk_rejects_invalid_bounds) {
    ProtoWorldDeltaGridChunk chunk;
    proto_world_delta_grid_chunk_init(&chunk);
    chunk.tick = 1;
    chunk.width = 32;
    chunk.height = 32;
    chunk.total_cells = 32u * 32u;
    chunk.start_index = chunk.total_cells - 4u;
    chunk.cell_count = 8u;
    chunk.final_chunk = true;
    chunk.cells = (uint16_t*)calloc((size_t)chunk.cell_count, sizeof(uint16_t));
    ASSERT_NOT_NULL(chunk.cells);

    uint8_t* buffer = NULL;
    size_t len = 0;
    int result = protocol_serialize_world_delta_grid_chunk(&chunk, &buffer, &len);
    ASSERT_EQ(result, -1);

    proto_world_delta_grid_chunk_free(&chunk);
}

// ============================================================================
// Colony Name Tests
// ============================================================================

TEST(maximum_length_colony_name) {
    ProtoColony colony;
    memset(&colony, 0, sizeof(ProtoColony));
    colony.id = 1;
    
    // Fill name to max length
    for (int i = 0; i < MAX_COLONY_NAME - 1; i++) {
        colony.name[i] = 'A' + (i % 26);
    }
    colony.name[MAX_COLONY_NAME - 1] = '\0';
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    ProtoColony deserialized;
    size = protocol_deserialize_colony(buffer, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    
    ASSERT_EQ(strlen(deserialized.name), MAX_COLONY_NAME - 1);
}

TEST(special_characters_in_names) {
    ProtoColony colony;
    memset(&colony, 0, sizeof(ProtoColony));
    colony.id = 42;
    strncpy(colony.name, "Test!@#$%^&*()_+-=[]", MAX_COLONY_NAME - 1);
    colony.name[MAX_COLONY_NAME - 1] = '\0';
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    ProtoColony deserialized;
    size = protocol_deserialize_colony(buffer, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    
    ASSERT_EQ(strcmp(colony.name, deserialized.name), 0);
}

TEST(unicode_like_bytes_in_names) {
    ProtoColony colony;
    memset(&colony, 0, sizeof(ProtoColony));
    colony.id = 1;
    
    // High byte values (would be UTF-8 multi-byte chars)
    colony.name[0] = (char)0xC3;
    colony.name[1] = (char)0xA9;  // é in UTF-8
    colony.name[2] = (char)0x00;
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    ProtoColony deserialized;
    size = protocol_deserialize_colony(buffer, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
}

TEST(empty_colony_name) {
    ProtoColony colony;
    memset(&colony, 0, sizeof(ProtoColony));
    colony.id = 1;
    colony.name[0] = '\0';  // Empty name
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    ProtoColony deserialized;
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
        size = protocol_deserialize_command(buffer, &deserialized, NULL);
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
    size = protocol_deserialize_command(buffer, &cmd, &deserialized);
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
    size = protocol_deserialize_command(buffer, &cmd, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    ASSERT_EQ(cmd, CMD_SPAWN_COLONY);
    
    // Float comparison with tolerance
    ASSERT(fabsf(deserialized.x - 123.456f) < 0.001f, "X should match");
    ASSERT(fabsf(deserialized.y - 789.012f) < 0.001f, "Y should match");
}

TEST(command_wire_examples_match_spec) {
    uint8_t pause_buffer[4];
    uint8_t expected_pause[] = {0x00, 0x00, 0x00, 0x00};
    int size = protocol_serialize_command(CMD_PAUSE, NULL, pause_buffer);
    ASSERT_EQ(size, 4);
    ASSERT_BYTES_EQ(pause_buffer, expected_pause, sizeof(expected_pause), "CMD_PAUSE bytes should match spec example");

    CommandSelectColony select = {.colony_id = 42};
    uint8_t select_buffer[8];
    uint8_t expected_select[] = {
        0x00, 0x00, 0x00, 0x05,
        0x00, 0x00, 0x00, 0x2A,
    };
    size = protocol_serialize_command(CMD_SELECT_COLONY, &select, select_buffer);
    ASSERT_EQ(size, 8);
    ASSERT_BYTES_EQ(select_buffer, expected_select, sizeof(expected_select), "CMD_SELECT_COLONY bytes should match spec example");
}

TEST(world_state_without_grid_uses_fixed_prefix) {
    ProtoWorld world;
    proto_world_init(&world);
    world.width = 7;
    world.height = 9;
    world.tick = 123;
    world.colony_count = 0;
    world.paused = true;
    world.speed_multiplier = 1.5f;

    uint8_t* buffer = NULL;
    size_t len = 0;
    int result = protocol_serialize_world_state(&world, &buffer, &len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(buffer);
    ASSERT_EQ(len, (size_t)26);

    uint8_t expected_prefix[] = {
        0x00, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x09,
        0x00, 0x00, 0x00, 0x7B,
        0x00, 0x00, 0x00, 0x00,
        0x01,
        0x3F, 0xC0, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    ASSERT_BYTES_EQ(buffer, expected_prefix, sizeof(expected_prefix), "World-state prefix should match current wire format");

    ProtoWorld decoded;
    proto_world_init(&decoded);
    result = protocol_deserialize_world_state(buffer, len, &decoded);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(decoded.width, world.width);
    ASSERT_EQ(decoded.height, world.height);
    ASSERT_EQ(decoded.tick, world.tick);
    ASSERT_EQ(decoded.paused, world.paused);
    ASSERT(fabsf(decoded.speed_multiplier - world.speed_multiplier) < 0.0001f,
           "speed multiplier should survive roundtrip");
    ASSERT_EQ(decoded.has_grid, false);
    proto_world_free(&decoded);

    free(buffer);
    proto_world_free(&world);
}

TEST(grid_rle_raw_mode_roundtrip) {
    uint16_t grid[] = {1u, 2u, 3u, 4u, 5u, 6u};
    uint8_t* buffer = NULL;
    size_t len = 0;
    int result = protocol_serialize_grid_rle(grid, 6u, &buffer, &len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(buffer);
    ASSERT_EQ(buffer[4], 1);

    uint16_t decoded[6] = {0};
    result = protocol_deserialize_grid_rle(buffer, len, decoded, 6u);
    ASSERT_EQ(result, 0);
    for (size_t i = 0; i < 6; i++) {
        ASSERT_EQ(decoded[i], grid[i]);
    }

    free(buffer);
}

TEST(grid_rle_rejects_unknown_mode) {
    uint8_t buffer[] = {
        0x00, 0x00, 0x00, 0x04,
        0x02,
        0x00, 0x01, 0x00, 0x02,
    };
    uint16_t decoded[4] = {0};
    ASSERT_EQ(protocol_deserialize_grid_rle(buffer, sizeof(buffer), decoded, 4u), -1);
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
    
    ProtoColony colony;
    ASSERT_EQ(protocol_serialize_colony(&colony, NULL), -1);
    ASSERT_EQ(protocol_deserialize_colony(NULL, &colony), -1);
    ASSERT_EQ(protocol_deserialize_colony(buffer, NULL), -1);
    
    // World null checks
    uint8_t* buf_ptr = NULL;
    size_t len;
    ASSERT_EQ(protocol_serialize_world_state(NULL, &buf_ptr, &len), -1);
    
    ProtoWorld world;
    ASSERT_EQ(protocol_serialize_world_state(&world, NULL, &len), -1);
    ASSERT_EQ(protocol_serialize_world_state(&world, &buf_ptr, NULL), -1);
    
    ASSERT_EQ(protocol_deserialize_world_state(NULL, 100, &world), -1);
    ASSERT_EQ(protocol_deserialize_world_state(buffer, 100, NULL), -1);
}

// ============================================================================
// Boundary Values
// ============================================================================

TEST(extreme_float_values) {
    ProtoColony colony;
    memset(&colony, 0, sizeof(ProtoColony));
    colony.id = 1;
    strcpy(colony.name, "Test");
    colony.x = 999999.999f;
    colony.y = -999999.999f;
    colony.radius = 0.0f;
    colony.growth_rate = 1.0f;
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int size = protocol_serialize_colony(&colony, buffer);
    ASSERT(size > 0, "Serialization should succeed");
    
    ProtoColony deserialized;
    size = protocol_deserialize_colony(buffer, &deserialized);
    ASSERT(size > 0, "Deserialization should succeed");
    
    ASSERT(fabsf(deserialized.x - colony.x) < 0.01f, "X should match");
    ASSERT(fabsf(deserialized.y - colony.y) < 0.01f, "Y should match");
}

TEST(max_population_value) {
    ProtoColony colony;
    memset(&colony, 0, sizeof(ProtoColony));
    colony.id = 1;
    colony.population = 0xFFFFFFFF;  // Max uint32
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    protocol_serialize_colony(&colony, buffer);
    
    ProtoColony deserialized;
    protocol_deserialize_colony(buffer, &deserialized);
    
    ASSERT_EQ(deserialized.population, 0xFFFFFFFF);
}

TEST(colony_detail_roundtrip) {
    ProtoColonyDetail detail;
    memset(&detail, 0, sizeof(detail));
    detail.base.id = 7;
    strcpy(detail.base.name, "Detailus smartii");
    detail.base.x = 42.0f;
    detail.base.y = 18.0f;
    detail.base.radius = 6.5f;
    detail.base.population = 123;
    detail.base.max_population = 456;
    detail.base.growth_rate = 0.33f;
    detail.base.color_r = 10;
    detail.base.color_g = 20;
    detail.base.color_b = 30;
    detail.base.alive = true;
    detail.tick = 99;
    detail.age = 12;
    detail.parent_id = 3;
    detail.state = 2;
    detail.flags = COLONY_DETAIL_FLAG_DORMANT;
    detail.stress_level = 0.75f;
    detail.biofilm_strength = 0.25f;
    detail.signal_strength = 0.60f;
    detail.drift_x = 0.12f;
    detail.drift_y = -0.34f;
    detail.behavior_mode = PROTO_COLONY_BEHAVIOR_MODE_RAIDING;
    detail.focus_direction = 2;
    detail.dominant_sensor = PROTO_COLONY_SENSOR_PRESSURE;
    detail.dominant_drive = PROTO_COLONY_DRIVE_HOSTILITY;
    detail.secondary_sensor = PROTO_COLONY_SENSOR_FRONTIER;
    detail.secondary_drive = PROTO_COLONY_DRIVE_GROWTH;
    detail.sensor_link_sensor = PROTO_COLONY_SENSOR_PRESSURE;
    detail.sensor_link_drive = PROTO_COLONY_DRIVE_HOSTILITY;
    detail.action_link_drive = PROTO_COLONY_DRIVE_HOSTILITY;
    detail.action_link_action = PROTO_COLONY_ACTION_ATTACK;
    detail.secondary_sensor_link_sensor = PROTO_COLONY_SENSOR_ALARM;
    detail.secondary_sensor_link_drive = PROTO_COLONY_DRIVE_CAUTION;
    detail.secondary_action_link_drive = PROTO_COLONY_DRIVE_GROWTH;
    detail.secondary_action_link_action = PROTO_COLONY_ACTION_EXPAND;
    detail.dominant_sensor_value = 0.91f;
    detail.dominant_drive_value = 0.78f;
    detail.secondary_sensor_value = 0.66f;
    detail.secondary_drive_value = 0.58f;
    detail.sensor_link_value = 0.72f;
    detail.action_link_value = 0.68f;
    detail.secondary_sensor_link_value = 0.44f;
    detail.secondary_action_link_value = 0.39f;
    detail.action_expand = 0.45f;
    detail.action_attack = 0.82f;
    detail.action_defend = 0.30f;
    detail.action_signal = 0.15f;
    detail.action_transfer = 0.27f;
    detail.action_dormancy = 0.11f;
    detail.action_motility = 0.36f;
    detail.trait_expansion = 0.80f;
    detail.trait_aggression = 0.90f;
    detail.trait_resilience = 0.70f;
    detail.trait_cooperation = 0.40f;
    detail.trait_efficiency = 0.55f;
    detail.trait_learning = 0.65f;

    uint8_t buffer[COLONY_DETAIL_SERIALIZED_SIZE];
    int size = protocol_serialize_colony_detail(&detail, buffer);
    ASSERT_EQ(size, COLONY_DETAIL_SERIALIZED_SIZE);

    ProtoColonyDetail decoded;
    memset(&decoded, 0, sizeof(decoded));
    size = protocol_deserialize_colony_detail(buffer, &decoded);
    ASSERT_EQ(size, COLONY_DETAIL_SERIALIZED_SIZE);
    ASSERT_EQ(decoded.base.id, detail.base.id);
    ASSERT_EQ(strcmp(decoded.base.name, detail.base.name), 0);
    ASSERT_EQ(decoded.tick, detail.tick);
    ASSERT_EQ(decoded.age, detail.age);
    ASSERT_EQ(decoded.parent_id, detail.parent_id);
    ASSERT_EQ(decoded.state, detail.state);
    ASSERT_EQ(decoded.flags, detail.flags);
    ASSERT_EQ(decoded.behavior_mode, detail.behavior_mode);
    ASSERT_EQ(decoded.focus_direction, detail.focus_direction);
    ASSERT_EQ(decoded.dominant_sensor, detail.dominant_sensor);
    ASSERT_EQ(decoded.dominant_drive, detail.dominant_drive);
    ASSERT_EQ(decoded.secondary_sensor, detail.secondary_sensor);
    ASSERT_EQ(decoded.secondary_drive, detail.secondary_drive);
    ASSERT_EQ(decoded.sensor_link_sensor, detail.sensor_link_sensor);
    ASSERT_EQ(decoded.sensor_link_drive, detail.sensor_link_drive);
    ASSERT_EQ(decoded.action_link_drive, detail.action_link_drive);
    ASSERT_EQ(decoded.action_link_action, detail.action_link_action);
    ASSERT_EQ(decoded.secondary_sensor_link_sensor, detail.secondary_sensor_link_sensor);
    ASSERT_EQ(decoded.secondary_sensor_link_drive, detail.secondary_sensor_link_drive);
    ASSERT_EQ(decoded.secondary_action_link_drive, detail.secondary_action_link_drive);
    ASSERT_EQ(decoded.secondary_action_link_action, detail.secondary_action_link_action);
    ASSERT(fabsf(decoded.stress_level - detail.stress_level) < 0.0001f, "stress roundtrip");
    ASSERT(fabsf(decoded.dominant_drive_value - detail.dominant_drive_value) < 0.0001f, "drive roundtrip");
    ASSERT(fabsf(decoded.secondary_sensor_value - detail.secondary_sensor_value) < 0.0001f, "sensor2 roundtrip");
    ASSERT(fabsf(decoded.sensor_link_value - detail.sensor_link_value) < 0.0001f, "sensor link roundtrip");
    ASSERT(fabsf(decoded.secondary_action_link_value - detail.secondary_action_link_value) < 0.0001f, "action link2 roundtrip");
    ASSERT(fabsf(decoded.action_attack - detail.action_attack) < 0.0001f, "attack roundtrip");
    ASSERT(fabsf(decoded.action_motility - detail.action_motility) < 0.0001f, "motility roundtrip");
    ASSERT(fabsf(decoded.trait_learning - detail.trait_learning) < 0.0001f, "learning roundtrip");
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
    RUN_TEST(header_wire_bytes_match_spec_example);
    RUN_TEST(malformed_header_wrong_magic);
    RUN_TEST(header_all_message_types);
    
    printf("\nPayload Tests:\n");
    RUN_TEST(zero_length_payload);
    RUN_TEST(partial_message_handling);
    RUN_TEST(world_delta_grid_chunk_roundtrip);
    RUN_TEST(world_delta_grid_chunk_wire_format);
    RUN_TEST(world_delta_grid_chunk_rejects_invalid_bounds);
    RUN_TEST(world_state_without_grid_uses_fixed_prefix);
    RUN_TEST(grid_rle_raw_mode_roundtrip);
    RUN_TEST(grid_rle_rejects_unknown_mode);
    
    printf("\nColony Name Tests:\n");
    RUN_TEST(maximum_length_colony_name);
    RUN_TEST(special_characters_in_names);
    RUN_TEST(unicode_like_bytes_in_names);
    RUN_TEST(empty_colony_name);
    
    printf("\nCommand Tests:\n");
    RUN_TEST(all_command_types);
    RUN_TEST(select_colony_command);
    RUN_TEST(spawn_colony_command);
    RUN_TEST(command_wire_examples_match_spec);
    
    printf("\nNull Input Tests:\n");
    RUN_TEST(null_inputs_handled);
    
    printf("\nBoundary Values:\n");
    RUN_TEST(extreme_float_values);
    RUN_TEST(max_population_value);
    RUN_TEST(colony_detail_roundtrip);
    
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
