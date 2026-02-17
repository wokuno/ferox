#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>

#include "../src/shared/protocol.h"
#include "../src/shared/network.h"

// Test framework macros
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define TEST_ASSERT_EQ(a, b, msg) TEST_ASSERT((a) == (b), msg)
#define TEST_ASSERT_STR_EQ(a, b, msg) TEST_ASSERT(strcmp(a, b) == 0, msg)
#define TEST_ASSERT_FLOAT_EQ(a, b, msg) TEST_ASSERT(((a) - (b)) < 0.0001f && ((b) - (a)) < 0.0001f, msg)

#define RUN_TEST(test) do { \
    printf("Running %s... ", #test); \
    fflush(stdout); \
    if (test() == 0) { \
        printf("OK\n"); \
        passed++; \
    } else { \
        failed++; \
    } \
    total++; \
} while(0)

// Test: Message header serialization/deserialization roundtrip
int test_protocol_header_serialization_roundtrip(void) {
    MessageHeader original = {
        .magic = PROTOCOL_MAGIC,
        .type = MSG_WORLD_STATE,
        .payload_len = 12345,
        .sequence = 42
    };
    
    uint8_t buffer[MESSAGE_HEADER_SIZE];
    int serialized = protocol_serialize_header(&original, buffer);
    TEST_ASSERT(serialized == MESSAGE_HEADER_SIZE, "Serialize should return header size");
    
    MessageHeader decoded;
    int deserialized = protocol_deserialize_header(buffer, &decoded);
    TEST_ASSERT(deserialized == MESSAGE_HEADER_SIZE, "Deserialize should return header size");
    
    TEST_ASSERT_EQ(original.magic, decoded.magic, "Magic should match");
    TEST_ASSERT_EQ(original.type, decoded.type, "Type should match");
    TEST_ASSERT_EQ(original.payload_len, decoded.payload_len, "Payload length should match");
    TEST_ASSERT_EQ(original.sequence, decoded.sequence, "Sequence should match");
    
    return 0;
}

// Test: Invalid magic number should fail deserialization
int test_protocol_header_rejects_invalid_magic(void) {
    uint8_t buffer[MESSAGE_HEADER_SIZE] = {0};
    // Set invalid magic
    buffer[0] = 0xFF;
    buffer[1] = 0xFF;
    buffer[2] = 0xFF;
    buffer[3] = 0xFF;
    
    MessageHeader header;
    int result = protocol_deserialize_header(buffer, &header);
    TEST_ASSERT(result < 0, "Should fail with invalid magic");
    
    return 0;
}

// Test: Colony serialization/deserialization roundtrip
int test_protocol_colony_serialization_roundtrip(void) {
    proto_colony original = {
        .id = 1234,
        .x = 100.5f,
        .y = 200.75f,
        .radius = 50.25f,
        .population = 1000000,
        .growth_rate = 1.5f,
        .color_r = 255,
        .color_g = 128,
        .color_b = 64,
        .alive = true
    };
    strncpy(original.name, "TestColony", MAX_COLONY_NAME);
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int serialized = protocol_serialize_colony(&original, buffer);
    TEST_ASSERT(serialized > 0, "Serialize should return positive size");
    
    proto_colony decoded;
    int deserialized = protocol_deserialize_colony(buffer, &decoded);
    TEST_ASSERT(deserialized > 0, "Deserialize should return positive size");
    TEST_ASSERT_EQ(serialized, deserialized, "Serialized and deserialized sizes should match");
    
    TEST_ASSERT_EQ(original.id, decoded.id, "ID should match");
    TEST_ASSERT_STR_EQ(original.name, decoded.name, "Name should match");
    TEST_ASSERT_FLOAT_EQ(original.x, decoded.x, "X should match");
    TEST_ASSERT_FLOAT_EQ(original.y, decoded.y, "Y should match");
    TEST_ASSERT_FLOAT_EQ(original.radius, decoded.radius, "Radius should match");
    TEST_ASSERT_EQ(original.population, decoded.population, "Population should match");
    TEST_ASSERT_FLOAT_EQ(original.growth_rate, decoded.growth_rate, "Growth rate should match");
    TEST_ASSERT_EQ(original.color_r, decoded.color_r, "Color R should match");
    TEST_ASSERT_EQ(original.color_g, decoded.color_g, "Color G should match");
    TEST_ASSERT_EQ(original.color_b, decoded.color_b, "Color B should match");
    TEST_ASSERT_EQ(original.alive, decoded.alive, "Alive should match");
    
    return 0;
}

// Test: proto_world state serialization/deserialization roundtrip
int test_protocol_world_state_roundtrip(void) {
    proto_world original;
    memset(&original, 0, sizeof(original));
    original.width = 1920;
    original.height = 1080;
    original.tick = 999999;
    original.colony_count = 2;
    original.paused = true;
    original.speed_multiplier = 2.5f;
    
    // Setup colony 1
    original.colonies[0].id = 1;
    strncpy(original.colonies[0].name, "Alpha", MAX_COLONY_NAME);
    original.colonies[0].x = 100.0f;
    original.colonies[0].y = 200.0f;
    original.colonies[0].radius = 30.0f;
    original.colonies[0].population = 5000;
    original.colonies[0].growth_rate = 1.1f;
    original.colonies[0].color_r = 255;
    original.colonies[0].color_g = 0;
    original.colonies[0].color_b = 0;
    original.colonies[0].alive = true;
    
    // Setup colony 2
    original.colonies[1].id = 2;
    strncpy(original.colonies[1].name, "Beta", MAX_COLONY_NAME);
    original.colonies[1].x = 500.0f;
    original.colonies[1].y = 600.0f;
    original.colonies[1].radius = 45.0f;
    original.colonies[1].population = 10000;
    original.colonies[1].growth_rate = 0.9f;
    original.colonies[1].color_r = 0;
    original.colonies[1].color_g = 255;
    original.colonies[1].color_b = 0;
    original.colonies[1].alive = true;
    
    uint8_t* buffer = NULL;
    size_t len = 0;
    int result = protocol_serialize_world_state(&original, &buffer, &len);
    TEST_ASSERT(result == 0, "Serialize should succeed");
    TEST_ASSERT(buffer != NULL, "Buffer should be allocated");
    TEST_ASSERT(len > 0, "Length should be positive");
    
    proto_world decoded;
    memset(&decoded, 0, sizeof(decoded));
    result = protocol_deserialize_world_state(buffer, len, &decoded);
    TEST_ASSERT(result == 0, "Deserialize should succeed");
    
    TEST_ASSERT_EQ(original.width, decoded.width, "Width should match");
    TEST_ASSERT_EQ(original.height, decoded.height, "Height should match");
    TEST_ASSERT_EQ(original.tick, decoded.tick, "Tick should match");
    TEST_ASSERT_EQ(original.colony_count, decoded.colony_count, "Colony count should match");
    TEST_ASSERT_EQ(original.paused, decoded.paused, "Paused should match");
    TEST_ASSERT_FLOAT_EQ(original.speed_multiplier, decoded.speed_multiplier, "Speed multiplier should match");
    
    // Check colonies
    TEST_ASSERT_EQ(original.colonies[0].id, decoded.colonies[0].id, "Colony 0 ID should match");
    TEST_ASSERT_STR_EQ(original.colonies[0].name, decoded.colonies[0].name, "Colony 0 name should match");
    TEST_ASSERT_EQ(original.colonies[1].id, decoded.colonies[1].id, "Colony 1 ID should match");
    TEST_ASSERT_STR_EQ(original.colonies[1].name, decoded.colonies[1].name, "Colony 1 name should match");
    
    free(buffer);
    return 0;
}

// Test: Command serialization/deserialization
int test_protocol_command_serialization_roundtrip(void) {
    // Test simple command without data
    uint8_t buffer[128];
    int size = protocol_serialize_command(CMD_PAUSE, NULL, buffer);
    TEST_ASSERT(size > 0, "Serialize pause command should succeed");
    
    CommandType cmd;
    int result = protocol_deserialize_command(buffer, &cmd, NULL);
    TEST_ASSERT(result > 0, "Deserialize pause command should succeed");
    TEST_ASSERT_EQ(cmd, CMD_PAUSE, "Command type should be PAUSE");
    
    // Test select colony command
    CommandSelectColony select_data = { .colony_id = 42 };
    size = protocol_serialize_command(CMD_SELECT_COLONY, &select_data, buffer);
    TEST_ASSERT(size > 0, "Serialize select command should succeed");
    
    CommandSelectColony decoded_select;
    result = protocol_deserialize_command(buffer, &cmd, &decoded_select);
    TEST_ASSERT(result > 0, "Deserialize select command should succeed");
    TEST_ASSERT_EQ(cmd, CMD_SELECT_COLONY, "Command type should be SELECT_COLONY");
    TEST_ASSERT_EQ(decoded_select.colony_id, 42, "Colony ID should match");
    
    // Test spawn colony command
    CommandSpawnColony spawn_data = { .x = 123.5f, .y = 456.7f };
    strncpy(spawn_data.name, "NewColony", MAX_COLONY_NAME);
    size = protocol_serialize_command(CMD_SPAWN_COLONY, &spawn_data, buffer);
    TEST_ASSERT(size > 0, "Serialize spawn command should succeed");
    
    CommandSpawnColony decoded_spawn;
    result = protocol_deserialize_command(buffer, &cmd, &decoded_spawn);
    TEST_ASSERT(result > 0, "Deserialize spawn command should succeed");
    TEST_ASSERT_EQ(cmd, CMD_SPAWN_COLONY, "Command type should be SPAWN_COLONY");
    TEST_ASSERT_FLOAT_EQ(decoded_spawn.x, 123.5f, "Spawn X should match");
    TEST_ASSERT_FLOAT_EQ(decoded_spawn.y, 456.7f, "Spawn Y should match");
    TEST_ASSERT_STR_EQ(decoded_spawn.name, "NewColony", "Spawn name should match");
    
    return 0;
}

// Test: Server creation on available port
int test_net_server_creates_on_available_port(void) {
    // Create server on port 0 (let OS assign)
    net_server* server = net_server_create(0);
    TEST_ASSERT(server != NULL, "Server should be created");
    TEST_ASSERT(server->listening, "Server should be listening");
    TEST_ASSERT(server->port > 0, "Server should have assigned port");
    TEST_ASSERT(server->fd >= 0, "Server should have valid fd");
    
    uint16_t port = server->port;
    net_server_destroy(server);
    
    // Create server on specific port
    server = net_server_create(port);
    TEST_ASSERT(server != NULL, "Server should be created on specific port");
    TEST_ASSERT_EQ(server->port, port, "Server port should match");
    
    net_server_destroy(server);
    return 0;
}

// Thread data for client/server test
typedef struct {
    uint16_t port;
    int result;
    char error[256];
} ThreadData;

static void* server_thread_func(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    net_server* server = net_server_create(data->port);
    if (!server) {
        snprintf(data->error, sizeof(data->error), "Failed to create server");
        data->result = 1;
        return NULL;
    }
    data->port = server->port;  // Update with actual port
    
    // Accept connection (blocking)
    net_socket* client = net_server_accept(server);
    if (!client) {
        snprintf(data->error, sizeof(data->error), "Failed to accept");
        net_server_destroy(server);
        data->result = 1;
        return NULL;
    }
    
    // Receive data
    uint8_t buffer[256];
    int received = net_recv(client, buffer, sizeof(buffer));
    if (received <= 0) {
        snprintf(data->error, sizeof(data->error), "Failed to receive");
        net_socket_close(client);
        net_server_destroy(server);
        data->result = 1;
        return NULL;
    }
    
    // Echo back
    int sent = net_send(client, buffer, received);
    if (sent != received) {
        snprintf(data->error, sizeof(data->error), "Failed to send");
        net_socket_close(client);
        net_server_destroy(server);
        data->result = 1;
        return NULL;
    }
    
    net_socket_close(client);
    net_server_destroy(server);
    data->result = 0;
    return NULL;
}

// Test: Client connection to local server
int test_net_client_connects_to_local_server(void) {
    ThreadData thread_data = { .port = 0, .result = -1 };
    
    // Create server first to get port
    net_server* temp_server = net_server_create(0);
    TEST_ASSERT(temp_server != NULL, "Temp server should be created");
    thread_data.port = temp_server->port;
    net_server_destroy(temp_server);
    
    // Start server thread
    pthread_t server_thread;
    int err = pthread_create(&server_thread, NULL, server_thread_func, &thread_data);
    TEST_ASSERT(err == 0, "Should create server thread");
    
    // Give server time to start
    usleep(100000);  // 100ms
    
    // Connect client
    net_socket* client = net_client_connect("127.0.0.1", thread_data.port);
    TEST_ASSERT(client != NULL, "Client should connect");
    TEST_ASSERT(client->connected, "Client should be connected");
    
    net_socket_close(client);
    
    pthread_join(server_thread, NULL);
    
    if (thread_data.result != 0) {
        fprintf(stderr, "Server error: %s\n", thread_data.error);
    }
    
    return 0;
}

// Test: Send/receive data integrity
int test_net_send_recv_preserves_data_integrity(void) {
    ThreadData thread_data = { .port = 0, .result = -1 };
    
    // Create server first to get port
    net_server* temp_server = net_server_create(0);
    TEST_ASSERT(temp_server != NULL, "Temp server should be created");
    thread_data.port = temp_server->port;
    net_server_destroy(temp_server);
    
    // Start server thread
    pthread_t server_thread;
    int err = pthread_create(&server_thread, NULL, server_thread_func, &thread_data);
    TEST_ASSERT(err == 0, "Should create server thread");
    
    // Give server time to start
    usleep(100000);  // 100ms
    
    // Connect client
    net_socket* client = net_client_connect("127.0.0.1", thread_data.port);
    TEST_ASSERT(client != NULL, "Client should connect");
    
    // Send test data
    const char* test_message = "Hello, bacterial world!";
    size_t msg_len = strlen(test_message) + 1;
    int sent = net_send(client, (const uint8_t*)test_message, msg_len);
    TEST_ASSERT_EQ((size_t)sent, msg_len, "Should send all data");
    
    // Receive echo
    uint8_t buffer[256];
    int received = net_recv(client, buffer, sizeof(buffer));
    TEST_ASSERT_EQ((size_t)received, msg_len, "Should receive all data");
    TEST_ASSERT_STR_EQ((char*)buffer, test_message, "Data should match");
    
    net_socket_close(client);
    pthread_join(server_thread, NULL);
    
    TEST_ASSERT_EQ(thread_data.result, 0, "Server thread should succeed");
    
    return 0;
}

// Test: net_has_data function
int test_net_has_data_detects_available_data(void) {
    // Create a socket pair using server/client
    net_server* server = net_server_create(0);
    TEST_ASSERT(server != NULL, "Server should be created");
    
    // Simple inline accept in forked process
    pid_t pid = fork();
    if (pid == 0) {
        // Child: connect and send data
        usleep(50000);  // 50ms delay
        net_socket* client = net_client_connect("127.0.0.1", server->port);
        if (client) {
            uint8_t data = 0x42;
            net_send(client, &data, 1);
            usleep(100000);  // Wait before closing
            net_socket_close(client);
        }
        exit(0);
    }
    
    // Parent: accept and check has_data
    net_socket* conn = net_server_accept(server);
    TEST_ASSERT(conn != NULL, "Should accept connection");
    
    // Wait for data
    usleep(100000);  // 100ms
    bool has_data = net_has_data(conn);
    TEST_ASSERT(has_data, "Should have data available");
    
    // Read the data
    uint8_t buffer[1];
    int received = net_recv(conn, buffer, 1);
    TEST_ASSERT_EQ(received, 1, "Should receive 1 byte");
    TEST_ASSERT_EQ(buffer[0], 0x42, "Data should match");
    
    // Now should not have data
    usleep(50000);
    // Note: After connection close, has_data might still return true for EOF indicator
    
    net_socket_close(conn);
    net_server_destroy(server);
    
    int status;
    waitpid(pid, &status, 0);
    
    return 0;
}

// Test: Socket options
int test_net_socket_options_do_not_crash(void) {
    net_server* server = net_server_create(0);
    TEST_ASSERT(server != NULL, "Server should be created");
    
    pid_t pid = fork();
    if (pid == 0) {
        usleep(50000);
        net_socket* client = net_client_connect("127.0.0.1", server->port);
        if (client) {
            // Test setting options
            net_set_nonblocking(client, true);
            net_set_nodelay(client, true);
            net_set_nonblocking(client, false);
            net_set_nodelay(client, false);
            net_socket_close(client);
        }
        exit(0);
    }
    
    net_socket* conn = net_server_accept(server);
    TEST_ASSERT(conn != NULL, "Should accept connection");
    
    // These should not crash
    net_set_nonblocking(conn, true);
    net_set_nodelay(conn, true);
    net_set_nonblocking(conn, false);
    net_set_nodelay(conn, false);
    
    net_socket_close(conn);
    net_server_destroy(server);
    
    int status;
    waitpid(pid, &status, 0);
    
    return 0;
}

// Test: NULL pointer handling
int test_protocol_and_network_handle_null_safely(void) {
    // Protocol functions should handle NULL
    TEST_ASSERT(protocol_serialize_header(NULL, NULL) < 0, "Should fail with NULL");
    TEST_ASSERT(protocol_deserialize_header(NULL, NULL) < 0, "Should fail with NULL");
    TEST_ASSERT(protocol_serialize_colony(NULL, NULL) < 0, "Should fail with NULL");
    TEST_ASSERT(protocol_deserialize_colony(NULL, NULL) < 0, "Should fail with NULL");
    TEST_ASSERT(protocol_serialize_command(CMD_PAUSE, NULL, NULL) < 0, "Should fail with NULL buffer");
    TEST_ASSERT(protocol_deserialize_command(NULL, NULL, NULL) < 0, "Should fail with NULL");
    TEST_ASSERT(protocol_serialize_world_state(NULL, NULL, NULL) < 0, "Should fail with NULL");
    TEST_ASSERT(protocol_deserialize_world_state(NULL, 0, NULL) < 0, "Should fail with NULL");
    
    // Network functions should handle NULL
    net_server_destroy(NULL);  // Should not crash
    net_socket_close(NULL);    // Should not crash
    net_set_nonblocking(NULL, true);  // Should not crash
    net_set_nodelay(NULL, true);      // Should not crash
    
    TEST_ASSERT(net_server_accept(NULL) == NULL, "Should return NULL");
    TEST_ASSERT(net_client_connect(NULL, 0) == NULL, "Should return NULL");
    TEST_ASSERT(net_send(NULL, NULL, 0) < 0 || net_send(NULL, NULL, 0) == 0, "Should fail or return 0");
    TEST_ASSERT(net_recv(NULL, NULL, 0) < 0 || net_recv(NULL, NULL, 0) == 0, "Should fail or return 0");
    TEST_ASSERT(!net_has_data(NULL), "Should return false");
    
    return 0;
}

int main(void) {
    int total = 0;
    int passed = 0;
    int failed = 0;
    
    printf("=== Phase 4 Unit Tests ===\n\n");
    
    // Protocol tests
    printf("--- Protocol Tests ---\n");
    RUN_TEST(test_protocol_header_serialization_roundtrip);
    RUN_TEST(test_protocol_header_rejects_invalid_magic);
    RUN_TEST(test_protocol_colony_serialization_roundtrip);
    RUN_TEST(test_protocol_world_state_roundtrip);
    RUN_TEST(test_protocol_command_serialization_roundtrip);
    
    // Network tests
    printf("\n--- Network Tests ---\n");
    RUN_TEST(test_net_server_creates_on_available_port);
    RUN_TEST(test_net_client_connects_to_local_server);
    RUN_TEST(test_net_send_recv_preserves_data_integrity);
    RUN_TEST(test_net_has_data_detects_available_data);
    RUN_TEST(test_net_socket_options_do_not_crash);
    
    // Edge case tests
    printf("\n--- Edge Case Tests ---\n");
    RUN_TEST(test_protocol_and_network_handle_null_safely);
    
    printf("\n=== Results ===\n");
    printf("Passed: %d/%d\n", passed, total);
    if (failed > 0) {
        printf("Failed: %d\n", failed);
        return 1;
    }
    
    printf("All tests passed!\n");
    return 0;
}
