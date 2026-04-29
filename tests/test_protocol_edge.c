/**
 * test_protocol_edge.c - Protocol edge case tests
 * Tests serialization/deserialization edge cases and malformed input handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>

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

static int make_nonblocking_socket_pair(int fds[2]) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return -1;
    }

    for (int i = 0; i < 2; i++) {
        int flags = fcntl(fds[i], F_GETFL, 0);
        if (flags < 0 || fcntl(fds[i], F_SETFL, flags | O_NONBLOCK) != 0) {
            close(fds[0]);
            close(fds[1]);
            return -1;
        }
    }

    return 0;
}

static void close_socket_pair(int fds[2]) {
    close(fds[0]);
    close(fds[1]);
}

static int drain_available_bytes(int fd, uint8_t* buffer, size_t capacity, size_t* received) {
    while (*received < capacity) {
        ssize_t n = read(fd, buffer + *received, capacity - *received);
        if (n > 0) {
            *received += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }

    return 0;
}

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

TEST(nonblocking_recv_handles_fragmented_frame) {
    int fds[2];
    ASSERT_EQ(make_nonblocking_socket_pair(fds), 0);

    const uint8_t source_payload[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    uint8_t* frame = NULL;
    size_t frame_len = 0;
    int result = protocol_build_message(MSG_COMMAND, source_payload, sizeof(source_payload), &frame, &frame_len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(frame);

    ProtocolRecvState state;
    protocol_recv_state_init(&state);
    MessageHeader header;
    uint8_t* payload = NULL;

    ASSERT_EQ(write(fds[0], frame, 3), 3);
    result = protocol_recv_message_nonblocking(fds[1], &state, &header, &payload);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(state.header_received, 3);
    ASSERT_EQ(payload, NULL);

    size_t header_remainder = MESSAGE_HEADER_SIZE - 3;
    ASSERT_EQ(write(fds[0], frame + 3, header_remainder), (ssize_t)header_remainder);
    result = protocol_recv_message_nonblocking(fds[1], &state, &header, &payload);
    ASSERT_EQ(result, 0);
    ASSERT_TRUE(state.header_ready);
    ASSERT_EQ(state.header.payload_len, sizeof(source_payload));
    ASSERT_EQ(payload, NULL);

    ASSERT_EQ(write(fds[0], frame + MESSAGE_HEADER_SIZE, 2), 2);
    result = protocol_recv_message_nonblocking(fds[1], &state, &header, &payload);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(state.payload_received, 2);
    ASSERT_EQ(payload, NULL);

    size_t payload_remainder = sizeof(source_payload) - 2;
    ASSERT_EQ(write(fds[0], frame + MESSAGE_HEADER_SIZE + 2, payload_remainder), (ssize_t)payload_remainder);
    result = protocol_recv_message_nonblocking(fds[1], &state, &header, &payload);
    ASSERT_EQ(result, 1);
    ASSERT_EQ(header.type, MSG_COMMAND);
    ASSERT_EQ(header.payload_len, sizeof(source_payload));
    ASSERT_NOT_NULL(payload);
    ASSERT_EQ(memcmp(payload, source_payload, sizeof(source_payload)), 0);

    free(payload);
    free(frame);
    protocol_recv_state_free(&state);
    close_socket_pair(fds);
}

TEST(nonblocking_recv_handles_zero_payload) {
    int fds[2];
    ASSERT_EQ(make_nonblocking_socket_pair(fds), 0);

    uint8_t* frame = NULL;
    size_t frame_len = 0;
    int result = protocol_build_message(MSG_ACK, NULL, 0, &frame, &frame_len);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(write(fds[0], frame, frame_len), (ssize_t)frame_len);

    ProtocolRecvState state;
    protocol_recv_state_init(&state);
    MessageHeader header;
    uint8_t* payload = (uint8_t*)0x1;
    result = protocol_recv_message_nonblocking(fds[1], &state, &header, &payload);
    ASSERT_EQ(result, 1);
    ASSERT_EQ(header.type, MSG_ACK);
    ASSERT_EQ(header.payload_len, 0);
    ASSERT_EQ(payload, NULL);

    free(frame);
    protocol_recv_state_free(&state);
    close_socket_pair(fds);
}

TEST(nonblocking_recv_rejects_oversized_payload) {
    int fds[2];
    ASSERT_EQ(make_nonblocking_socket_pair(fds), 0);

    MessageHeader bad_header = {
        .magic = PROTOCOL_MAGIC,
        .type = MSG_WORLD_STATE,
        .payload_len = MAX_PAYLOAD_SIZE + 1u,
        .sequence = 99u
    };
    uint8_t frame[MESSAGE_HEADER_SIZE];
    ASSERT_EQ(protocol_serialize_header(&bad_header, frame), MESSAGE_HEADER_SIZE);
    ASSERT_EQ(write(fds[0], frame, sizeof(frame)), (ssize_t)sizeof(frame));

    ProtocolRecvState state;
    protocol_recv_state_init(&state);
    MessageHeader header;
    uint8_t* payload = NULL;
    int result = protocol_recv_message_nonblocking(fds[1], &state, &header, &payload);
    ASSERT_EQ(result, -1);
    ASSERT_EQ(state.header_received, 0);
    ASSERT_EQ(payload, NULL);

    protocol_recv_state_free(&state);
    close_socket_pair(fds);
}

TEST(nonblocking_recv_rejects_eof_mid_frame) {
    int fds[2];
    ASSERT_EQ(make_nonblocking_socket_pair(fds), 0);

    uint8_t* frame = NULL;
    size_t frame_len = 0;
    int result = protocol_build_message(MSG_ERROR, (const uint8_t*)"err", 3, &frame, &frame_len);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(write(fds[0], frame, 4), 4);
    close(fds[0]);
    fds[0] = -1;

    ProtocolRecvState state;
    protocol_recv_state_init(&state);
    MessageHeader header;
    uint8_t* payload = NULL;
    result = protocol_recv_message_nonblocking(fds[1], &state, &header, &payload);
    ASSERT_EQ(result, -1);
    ASSERT_EQ(state.header_received, 0);
    ASSERT_EQ(payload, NULL);

    free(frame);
    protocol_recv_state_free(&state);
    close(fds[1]);
}

TEST(nonblocking_send_frame_writes_complete_frame) {
    int fds[2];
    ASSERT_EQ(make_nonblocking_socket_pair(fds), 0);

    const uint8_t source_payload[] = {1, 2, 3, 4};
    uint8_t* frame = NULL;
    size_t frame_len = 0;
    int result = protocol_build_message(MSG_COLONY_INFO, source_payload, sizeof(source_payload), &frame, &frame_len);
    ASSERT_EQ(result, 0);

    size_t offset = 0;
    result = protocol_send_frame_nonblocking(fds[0], frame, frame_len, &offset);
    ASSERT_EQ(result, 1);
    ASSERT_EQ(offset, frame_len);

    uint8_t received[MESSAGE_HEADER_SIZE + sizeof(source_payload)];
    ssize_t n = read(fds[1], received, sizeof(received));
    ASSERT_EQ(n, (ssize_t)sizeof(received));
    ASSERT_EQ(memcmp(received, frame, frame_len), 0);

    free(frame);
    close_socket_pair(fds);
}

TEST(nonblocking_send_frame_handles_backpressure_and_resume) {
    int fds[2];
    ASSERT_EQ(make_nonblocking_socket_pair(fds), 0);

    int send_buffer_size = 4096;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size));

    uint8_t* source_payload = (uint8_t*)malloc(MAX_PAYLOAD_SIZE);
    ASSERT_NOT_NULL(source_payload);
    for (size_t i = 0; i < MAX_PAYLOAD_SIZE; i++) {
        source_payload[i] = (uint8_t)(i & 0xFFu);
    }

    uint8_t* frame = NULL;
    size_t frame_len = 0;
    int result = protocol_build_message(MSG_WORLD_STATE, source_payload, MAX_PAYLOAD_SIZE, &frame, &frame_len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(frame);

    size_t offset = 0;
    result = protocol_send_frame_nonblocking(fds[0], frame, frame_len, &offset);
    ASSERT_EQ(result, 0);
    ASSERT(offset > 0 && offset < frame_len, "Backpressure should leave partial send progress");

    uint8_t* received = (uint8_t*)malloc(frame_len);
    ASSERT_NOT_NULL(received);
    size_t received_len = 0;
    ASSERT_EQ(drain_available_bytes(fds[1], received, frame_len, &received_len), 0);

    for (int iterations = 0; offset < frame_len && iterations < 10000; iterations++) {
        result = protocol_send_frame_nonblocking(fds[0], frame, frame_len, &offset);
        ASSERT(result >= 0, "Send should not fail while peer is connected");
        ASSERT_EQ(drain_available_bytes(fds[1], received, frame_len, &received_len), 0);
    }

    ASSERT_EQ(offset, frame_len);
    ASSERT_EQ(received_len, frame_len);
    ASSERT_EQ(memcmp(received, frame, frame_len), 0);

    free(received);
    free(frame);
    free(source_payload);
    close_socket_pair(fds);
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

typedef struct TestGridRecord {
    uint8_t marker;
    uint32_t colony_id;
    uint8_t age;
} TestGridRecord;

TEST(world_delta_grid_chunk_u32_field_matches_array_serializer) {
    uint16_t grid[64];
    TestGridRecord records[64];
    for (uint32_t i = 0; i < 64; i++) {
        grid[i] = (uint16_t)((i * 31u) & 0xFFFFu);
        records[i].marker = 0xA5u;
        records[i].colony_id = (uint32_t)grid[i];
        records[i].age = (uint8_t)i;
    }
    grid[12] = 7u;
    records[12].colony_id = 0x10007u;

    ProtoWorldDeltaGridChunk chunk;
    proto_world_delta_grid_chunk_init(&chunk);
    chunk.tick = 91;
    chunk.width = 8;
    chunk.height = 8;
    chunk.total_cells = 64;
    chunk.start_index = 9;
    chunk.cell_count = 27;
    chunk.final_chunk = false;
    chunk.cells = &grid[chunk.start_index];

    uint8_t* array_buffer = NULL;
    size_t array_len = 0;
    int result = protocol_serialize_world_delta_grid_chunk(&chunk, &array_buffer, &array_len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(array_buffer);

    uint8_t* field_buffer = NULL;
    size_t field_len = 0;
    result = protocol_serialize_world_delta_grid_chunk_from_u32_field(chunk.tick,
                                                                      chunk.width,
                                                                      chunk.height,
                                                                      chunk.total_cells,
                                                                      chunk.start_index,
                                                                      chunk.cell_count,
                                                                      chunk.final_chunk,
                                                                      records,
                                                                      sizeof(records[0]),
                                                                      offsetof(TestGridRecord, colony_id),
                                                                      &field_buffer,
                                                                      &field_len);
    ASSERT_EQ(result, 0);
    ASSERT_NOT_NULL(field_buffer);
    ASSERT_EQ(field_len, array_len);
    ASSERT_EQ(memcmp(field_buffer, array_buffer, array_len), 0);

    ProtoWorldDeltaGridChunk decoded;
    proto_world_delta_grid_chunk_init(&decoded);
    result = protocol_deserialize_world_delta_grid_chunk(field_buffer, field_len, &decoded);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(decoded.start_index, chunk.start_index);
    ASSERT_EQ(decoded.cell_count, chunk.cell_count);
    for (uint32_t i = 0; i < decoded.cell_count; i++) {
        ASSERT_EQ(decoded.cells[i], grid[chunk.start_index + i]);
    }

    free(array_buffer);
    free(field_buffer);
    proto_world_delta_grid_chunk_free(&decoded);
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

TEST(command_rejects_short_command_word_payloads) {
    uint8_t buffer[COMMAND_SPAWN_COLONY_SERIALIZED_SIZE];
    memset(buffer, 0xAA, sizeof(buffer));

    size_t lengths[] = {0, 1, 3};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        CommandType cmd = CMD_RESET;
        CommandSelectColony data = { .colony_id = 0x12345678u };

        int size = protocol_deserialize_command(buffer, lengths[i], &cmd, &data);
        ASSERT_EQ(size, -1);
        ASSERT_EQ(cmd, CMD_RESET);
        ASSERT_EQ(data.colony_id, 0x12345678u);
    }
}

TEST(command_rejects_short_select_colony_payloads) {
    CommandSelectColony data = { .colony_id = 12345u };
    uint8_t buffer[COMMAND_SELECT_COLONY_SERIALIZED_SIZE];
    int full_size = protocol_serialize_command(CMD_SELECT_COLONY, &data, buffer);
    ASSERT_EQ(full_size, COMMAND_SELECT_COLONY_SERIALIZED_SIZE);

    size_t lengths[] = {4, 5, 7};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        CommandType cmd = CMD_RESET;
        CommandSelectColony decoded = { .colony_id = 0x87654321u };

        int size = protocol_deserialize_command(buffer, lengths[i], &cmd, &decoded);
        ASSERT_EQ(size, -1);
        ASSERT_EQ(cmd, CMD_RESET);
        ASSERT_EQ(decoded.colony_id, 0x87654321u);
    }
}

TEST(command_rejects_truncated_spawn_colony_payloads) {
    CommandSpawnColony data = {
        .x = 12.5f,
        .y = 34.5f
    };
    strncpy(data.name, "TruncatedSpawn", MAX_COLONY_NAME);

    uint8_t buffer[COMMAND_SPAWN_COLONY_SERIALIZED_SIZE];
    int full_size = protocol_serialize_command(CMD_SPAWN_COLONY, &data, buffer);
    ASSERT_EQ(full_size, COMMAND_SPAWN_COLONY_SERIALIZED_SIZE);

    size_t lengths[] = {
        4,
        5,
        7,
        COMMAND_TYPE_SERIALIZED_SIZE + 4,
        COMMAND_TYPE_SERIALIZED_SIZE + 8,
        COMMAND_SPAWN_COLONY_SERIALIZED_SIZE - 1
    };
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        CommandType cmd = CMD_RESET;
        CommandSpawnColony decoded;
        memset(&decoded, 0xCD, sizeof(decoded));

        int size = protocol_deserialize_command(buffer, lengths[i], &cmd, &decoded);
        ASSERT_EQ(size, -1);
        ASSERT_EQ(cmd, CMD_RESET);
        ASSERT_EQ(decoded.name[0], (char)0xCD);
    }
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
    RUN_TEST(malformed_header_wrong_magic);
    RUN_TEST(header_all_message_types);
    
    printf("\nPayload Tests:\n");
    RUN_TEST(zero_length_payload);
    RUN_TEST(partial_message_handling);
    RUN_TEST(nonblocking_recv_handles_fragmented_frame);
    RUN_TEST(nonblocking_recv_handles_zero_payload);
    RUN_TEST(nonblocking_recv_rejects_oversized_payload);
    RUN_TEST(nonblocking_recv_rejects_eof_mid_frame);
    RUN_TEST(nonblocking_send_frame_writes_complete_frame);
    RUN_TEST(nonblocking_send_frame_handles_backpressure_and_resume);
    RUN_TEST(world_delta_grid_chunk_roundtrip);
    RUN_TEST(world_delta_grid_chunk_u32_field_matches_array_serializer);
    RUN_TEST(world_delta_grid_chunk_rejects_invalid_bounds);
    
    printf("\nColony Name Tests:\n");
    RUN_TEST(maximum_length_colony_name);
    RUN_TEST(special_characters_in_names);
    RUN_TEST(unicode_like_bytes_in_names);
    RUN_TEST(empty_colony_name);
    
    printf("\nCommand Tests:\n");
    RUN_TEST(all_command_types);
    RUN_TEST(select_colony_command);
    RUN_TEST(spawn_colony_command);
    RUN_TEST(command_rejects_short_command_word_payloads);
    RUN_TEST(command_rejects_short_select_colony_payloads);
    RUN_TEST(command_rejects_truncated_spawn_colony_payloads);
    
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
