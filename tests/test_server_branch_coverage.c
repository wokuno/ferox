#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/server/server.h"

static int tests_passed = 0;
static int tests_failed = 0;
static int current_test_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    fflush(stdout); \
    current_test_failed = 0; \
    test_##name(); \
    if (!current_test_failed) { \
        printf("OK\n"); \
        tests_passed++; \
    } \
} while (0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL\n  %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        current_test_failed = 1; \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) ASSERT((cond), #cond)
#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_NE(a, b) ASSERT((a) != (b), #a " != " #b)

static net_socket* make_mock_socket(bool connected) {
    net_socket* socket = (net_socket*)calloc(1, sizeof(net_socket));
    if (!socket) {
        return NULL;
    }
    socket->fd = -1;
    socket->connected = connected;
    return socket;
}

TEST(server_handle_command_clamps_speed_limits) {
    Server* server = server_create(0, 20, 20, 2);
    ASSERT_TRUE(server != NULL);

    net_socket* socket = make_mock_socket(true);
    ASSERT_TRUE(socket != NULL);
    client_session* client = server_add_client(server, socket);
    ASSERT_TRUE(client != NULL);

    server->speed_multiplier = 99.0f;
    server_handle_command(server, client, CMD_SPEED_UP, NULL);
    ASSERT_TRUE(server->speed_multiplier == 100.0f);

    server_handle_command(server, client, CMD_SPEED_UP, NULL);
    ASSERT_TRUE(server->speed_multiplier == 100.0f);

    server->speed_multiplier = 0.11f;
    server_handle_command(server, client, CMD_SLOW_DOWN, NULL);
    ASSERT_TRUE(server->speed_multiplier == 0.1f);

    server_handle_command(server, client, CMD_SLOW_DOWN, NULL);
    ASSERT_TRUE(server->speed_multiplier == 0.1f);

    server_destroy(server);
}

TEST(server_handle_command_reset_rebuilds_world) {
    Server* server = server_create(0, 21, 13, 2);
    ASSERT_TRUE(server != NULL);

    net_socket* socket = make_mock_socket(true);
    ASSERT_TRUE(socket != NULL);
    client_session* client = server_add_client(server, socket);
    ASSERT_TRUE(client != NULL);

    World* old_world = server->world;
    AtomicWorld* old_atomic = server->atomic_world;
    ParallelContext* old_parallel = server->parallel_ctx;

    server_handle_command(server, client, CMD_RESET, NULL);

    ASSERT_TRUE(server->world != NULL);
    ASSERT_TRUE(server->atomic_world != NULL);
    ASSERT_TRUE(server->parallel_ctx != NULL);
    ASSERT_NE(server->world, old_world);
    ASSERT_NE(server->atomic_world, old_atomic);
    ASSERT_NE(server->parallel_ctx, old_parallel);
    ASSERT_EQ(server->world->width, 21);
    ASSERT_EQ(server->world->height, 13);

    server_destroy(server);
}

TEST(server_handle_command_data_branches_for_select_and_spawn) {
    Server stub = {0};
    client_session client = {0};
    client.id = 77;

    server_handle_command(&stub, &client, CMD_SELECT_COLONY, NULL);
    ASSERT_EQ(client.selected_colony, 0u);

    CommandSelectColony select_colony = {.colony_id = 42};
    server_handle_command(&stub, &client, CMD_SELECT_COLONY, &select_colony);
    ASSERT_EQ(client.selected_colony, 42u);

    server_handle_command(&stub, &client, CMD_SPAWN_COLONY, NULL);

    CommandSpawnColony spawn = {.x = 2.0f, .y = 4.0f};
    server_handle_command(&stub, &client, CMD_SPAWN_COLONY, &spawn);
}

TEST(server_remove_client_noop_when_target_missing) {
    Server* server = server_create(0, 20, 20, 2);
    ASSERT_TRUE(server != NULL);

    net_socket* socket = make_mock_socket(true);
    ASSERT_TRUE(socket != NULL);
    client_session* tracked = server_add_client(server, socket);
    ASSERT_TRUE(tracked != NULL);
    ASSERT_EQ(server->client_count, 1);

    client_session ghost = {0};
    server_remove_client(server, &ghost);
    ASSERT_EQ(server->client_count, 1);

    server_destroy(server);
}

TEST(server_process_clients_skips_non_connected_clients) {
    Server* server = server_create(0, 20, 20, 2);
    ASSERT_TRUE(server != NULL);

    net_socket* inactive_socket = make_mock_socket(true);
    net_socket* disconnected_socket = make_mock_socket(false);
    ASSERT_TRUE(inactive_socket != NULL);
    ASSERT_TRUE(disconnected_socket != NULL);

    client_session* inactive_client = server_add_client(server, inactive_socket);
    client_session* disconnected_client = server_add_client(server, disconnected_socket);
    ASSERT_TRUE(inactive_client != NULL);
    ASSERT_TRUE(disconnected_client != NULL);

    inactive_client->active = false;
    disconnected_client->active = true;

    server_process_clients(server);
    ASSERT_EQ(server->client_count, 2);

    server_destroy(server);
}

TEST(server_stop_and_get_port_guard_branches) {
    Server* server = server_create(0, 20, 20, 2);
    ASSERT_TRUE(server != NULL);

    atomic_store(&server->running, false);
    server_stop(server);
    ASSERT_TRUE(!atomic_load(&server->running));

    atomic_store(&server->running, true);
    server_stop(server);
    ASSERT_TRUE(!atomic_load(&server->running));

    Server stub = {0};
    ASSERT_EQ(server_get_port(&stub), 0u);

    server_destroy(server);
}

int main(void) {
    printf("=== Server Branch Coverage Tests ===\n");

    RUN_TEST(server_handle_command_clamps_speed_limits);
    RUN_TEST(server_handle_command_reset_rebuilds_world);
    RUN_TEST(server_handle_command_data_branches_for_select_and_spawn);
    RUN_TEST(server_remove_client_noop_when_target_missing);
    RUN_TEST(server_process_clients_skips_non_connected_clients);
    RUN_TEST(server_stop_and_get_port_guard_branches);

    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
