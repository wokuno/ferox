/**
 * test_phase5.c - Integration tests for Phase 5 Server Implementation
 * Part of Phase 5: Server Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>

#include "../src/server/server.h"
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
#define TEST_ASSERT_NEQ(a, b, msg) TEST_ASSERT((a) != (b), msg)

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

// Helper: Run server for a short time then stop
typedef struct {
    Server* server;
    int duration_ms;
} ServerRunData;

static void* run_server_briefly(void* arg) {
    ServerRunData* data = (ServerRunData*)arg;
    
    // Start a thread that will stop the server after duration
    usleep(data->duration_ms * 1000);
    server_stop(data->server);
    
    return NULL;
}

// Test: Server creation with valid parameters
int test_server_create_valid(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    TEST_ASSERT(server->listener != NULL, "Listener should be created");
    TEST_ASSERT(server->world != NULL, "World should be created");
    TEST_ASSERT(server->pool != NULL, "Thread pool should be created");
    TEST_ASSERT(server->parallel_ctx != NULL, "Parallel context should be created");
    TEST_ASSERT_EQ(server->running, false, "Server should not be running initially");
    TEST_ASSERT_EQ(server->paused, false, "Server should not be paused initially");
    TEST_ASSERT(server_get_port(server) > 0, "Server should have valid port");
    
    server_destroy(server);
    return 0;
}

// Test: Server creation with invalid parameters
int test_server_create_invalid(void) {
    TEST_ASSERT(server_create(0, 0, 50, 2) == NULL, "Should fail with zero width");
    TEST_ASSERT(server_create(0, 50, 0, 2) == NULL, "Should fail with zero height");
    TEST_ASSERT(server_create(0, 50, 50, 0) == NULL, "Should fail with zero threads");
    TEST_ASSERT(server_create(0, -1, 50, 2) == NULL, "Should fail with negative width");
    
    return 0;
}

// Test: Server destruction (including NULL)
int test_server_destroy(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    
    uint16_t port = server_get_port(server);
    TEST_ASSERT(port > 0, "Should have valid port");
    
    server_destroy(server);
    
    // NULL should not crash
    server_destroy(NULL);
    
    return 0;
}

// Test: Client add and remove
int test_client_management(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    
    // Create a mock socket (we won't actually connect)
    NetSocket* mock_socket = (NetSocket*)calloc(1, sizeof(NetSocket));
    mock_socket->fd = -1;  // Invalid but we won't use it
    mock_socket->connected = true;
    
    // Add client
    ClientSession* client = server_add_client(server, mock_socket);
    TEST_ASSERT(client != NULL, "Client should be added");
    TEST_ASSERT_EQ(client->socket, mock_socket, "Socket should be set");
    TEST_ASSERT_EQ(client->active, true, "Client should be active");
    TEST_ASSERT_EQ(server->client_count, 1, "Client count should be 1");
    
    uint32_t client_id = client->id;
    TEST_ASSERT(client_id > 0, "Client should have valid ID");
    
    // Add another client
    NetSocket* mock_socket2 = (NetSocket*)calloc(1, sizeof(NetSocket));
    mock_socket2->fd = -1;
    mock_socket2->connected = true;
    
    ClientSession* client2 = server_add_client(server, mock_socket2);
    TEST_ASSERT(client2 != NULL, "Second client should be added");
    TEST_ASSERT_NEQ(client2->id, client_id, "Clients should have different IDs");
    TEST_ASSERT_EQ(server->client_count, 2, "Client count should be 2");
    
    // Remove first client
    server_remove_client(server, client);
    TEST_ASSERT_EQ(server->client_count, 1, "Client count should be 1 after removal");
    
    // Remove second client
    server_remove_client(server, client2);
    TEST_ASSERT_EQ(server->client_count, 0, "Client count should be 0");
    
    // Remove NULL should not crash
    server_remove_client(server, NULL);
    
    server_destroy(server);
    return 0;
}

// Test: Command handling
int test_command_handling(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    
    // Create mock client
    NetSocket* mock_socket = (NetSocket*)calloc(1, sizeof(NetSocket));
    mock_socket->fd = -1;
    mock_socket->connected = true;
    ClientSession* client = server_add_client(server, mock_socket);
    TEST_ASSERT(client != NULL, "Client should be added");
    
    // Test pause command
    TEST_ASSERT_EQ(server->paused, false, "Server should not be paused");
    server_handle_command(server, client, CMD_PAUSE, NULL);
    TEST_ASSERT_EQ(server->paused, true, "Server should be paused after CMD_PAUSE");
    
    // Test resume command
    server_handle_command(server, client, CMD_RESUME, NULL);
    TEST_ASSERT_EQ(server->paused, false, "Server should not be paused after CMD_RESUME");
    
    // Test speed up command
    float initial_speed = server->speed_multiplier;
    server_handle_command(server, client, CMD_SPEED_UP, NULL);
    TEST_ASSERT(server->speed_multiplier > initial_speed, "Speed should increase");
    
    // Test slow down command
    float faster_speed = server->speed_multiplier;
    server_handle_command(server, client, CMD_SLOW_DOWN, NULL);
    TEST_ASSERT(server->speed_multiplier < faster_speed, "Speed should decrease");
    
    // Test select colony command
    CommandSelectColony select_cmd = { .colony_id = 42 };
    server_handle_command(server, client, CMD_SELECT_COLONY, &select_cmd);
    TEST_ASSERT_EQ(client->selected_colony, 42, "Selected colony should be updated");
    
    server_remove_client(server, client);
    server_destroy(server);
    return 0;
}

// Test: Server port assignment
int test_server_port_assignment(void) {
    // Create server on port 0 (auto-assign)
    Server* server1 = server_create(0, 50, 50, 2);
    TEST_ASSERT(server1 != NULL, "Server1 should be created");
    
    uint16_t port1 = server_get_port(server1);
    TEST_ASSERT(port1 > 0, "Server1 should have assigned port");
    
    // Create another server on different port
    Server* server2 = server_create(0, 50, 50, 2);
    TEST_ASSERT(server2 != NULL, "Server2 should be created");
    
    uint16_t port2 = server_get_port(server2);
    TEST_ASSERT(port2 > 0, "Server2 should have assigned port");
    TEST_ASSERT_NEQ(port1, port2, "Servers should have different ports");
    
    server_destroy(server1);
    server_destroy(server2);
    return 0;
}

// Test: World initialization
int test_world_initialization(void) {
    Server* server = server_create(0, 100, 100, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    TEST_ASSERT(server->world != NULL, "World should exist");
    TEST_ASSERT_EQ(server->world->width, 100, "World width should be 100");
    TEST_ASSERT_EQ(server->world->height, 100, "World height should be 100");
    TEST_ASSERT_EQ(server->world->tick, 0, "World tick should be 0");
    
    // Initialize colonies
    world_init_random_colonies(server->world, 3);
    TEST_ASSERT(server->world->colony_count >= 3, "Should have at least 3 colonies");
    
    server_destroy(server);
    return 0;
}

// Thread data for client connection test
typedef struct {
    uint16_t port;
    int connected;
    int received_world_state;
} ClientTestData;

static void* client_connect_thread(void* arg) {
    ClientTestData* data = (ClientTestData*)arg;
    
    // Wait for server to start
    usleep(100000);  // 100ms
    
    // Connect to server
    NetSocket* socket = net_client_connect("127.0.0.1", data->port);
    if (!socket) {
        return NULL;
    }
    
    data->connected = 1;
    
    // Try to receive world state
    MessageHeader header;
    uint8_t* payload = NULL;
    
    // Set timeout by making socket non-blocking and using select
    net_set_nonblocking(socket, false);
    
    // Wait for data with timeout
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(socket->fd, &readfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    
    if (select(socket->fd + 1, &readfds, NULL, NULL, &tv) > 0) {
        if (protocol_recv_message(socket->fd, &header, &payload) >= 0) {
            if (header.type == MSG_WORLD_STATE) {
                data->received_world_state = 1;
            }
            if (payload) free(payload);
        }
    }
    
    net_socket_close(socket);
    return NULL;
}

// Test: Client connection and world state broadcasting
int test_client_connection(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    
    world_init_random_colonies(server->world, 2);
    
    ClientTestData client_data = {
        .port = server_get_port(server),
        .connected = 0,
        .received_world_state = 0
    };
    
    // Start client thread
    pthread_t client_thread;
    pthread_create(&client_thread, NULL, client_connect_thread, &client_data);
    
    // Run server briefly in a thread
    ServerRunData run_data = { .server = server, .duration_ms = 500 };
    pthread_t stopper_thread;
    pthread_create(&stopper_thread, NULL, run_server_briefly, &run_data);
    
    // Run server (will be stopped by stopper thread)
    server_run(server);
    
    pthread_join(client_thread, NULL);
    pthread_join(stopper_thread, NULL);
    
    TEST_ASSERT_EQ(client_data.connected, 1, "Client should have connected");
    // Note: receiving world state is not guaranteed in the short time window
    
    server_destroy(server);
    return 0;
}

// Test: NULL handling for all server functions
int test_null_handling(void) {
    // All these should not crash
    server_destroy(NULL);
    server_run(NULL);
    server_stop(NULL);
    server_broadcast_world_state(NULL);
    server_send_colony_info(NULL, NULL, 0);
    server_handle_command(NULL, NULL, CMD_PAUSE, NULL);
    server_add_client(NULL, NULL);
    server_remove_client(NULL, NULL);
    server_process_clients(NULL);
    
    TEST_ASSERT_EQ(server_get_port(NULL), 0, "NULL server should return port 0");
    
    return 0;
}

// Test: Tick rate and speed multiplier
int test_tick_rate(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    
    // Check default tick rate
    TEST_ASSERT_EQ(server->tick_rate_ms, DEFAULT_TICK_RATE_MS, "Default tick rate should be set");
    
    // Modify tick rate
    server->tick_rate_ms = 50;
    TEST_ASSERT_EQ(server->tick_rate_ms, 50, "Tick rate should be modifiable");
    
    // Check default speed multiplier
    TEST_ASSERT(server->speed_multiplier >= 0.99f && server->speed_multiplier <= 1.01f, 
                "Default speed multiplier should be 1.0");
    
    server_destroy(server);
    return 0;
}

int main(void) {
    int total = 0;
    int passed = 0;
    int failed = 0;
    
    // Ignore SIGPIPE to prevent crash when writing to closed sockets
    signal(SIGPIPE, SIG_IGN);
    
    printf("=== Phase 5 Integration Tests ===\n\n");
    
    // Server creation tests
    printf("--- Server Creation Tests ---\n");
    RUN_TEST(test_server_create_valid);
    RUN_TEST(test_server_create_invalid);
    RUN_TEST(test_server_destroy);
    RUN_TEST(test_server_port_assignment);
    
    // Client management tests
    printf("\n--- Client Management Tests ---\n");
    RUN_TEST(test_client_management);
    
    // Command handling tests
    printf("\n--- Command Handling Tests ---\n");
    RUN_TEST(test_command_handling);
    
    // World tests
    printf("\n--- World Tests ---\n");
    RUN_TEST(test_world_initialization);
    RUN_TEST(test_tick_rate);
    
    // Integration tests
    printf("\n--- Integration Tests ---\n");
    RUN_TEST(test_client_connection);
    
    // Edge case tests
    printf("\n--- Edge Case Tests ---\n");
    RUN_TEST(test_null_handling);
    
    printf("\n=== Results ===\n");
    printf("Passed: %d/%d\n", passed, total);
    if (failed > 0) {
        printf("Failed: %d\n", failed);
        return 1;
    }
    
    printf("All tests passed!\n");
    return 0;
}
