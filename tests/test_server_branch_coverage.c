#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

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

static NetSocket* make_mock_socket(bool connected, int fd) {
    NetSocket* socket = (NetSocket*)calloc(1, sizeof(NetSocket));
    if (!socket) {
        return NULL;
    }
    socket->fd = fd;
    socket->connected = connected;
    return socket;
}

static int read_command_status_message(int fd, MessageType* type, ProtoCommandStatus* status) {
    MessageHeader header;
    uint8_t* payload = NULL;
    int result = protocol_recv_message(fd, &header, &payload);
    if (result < 0) {
        return -1;
    }

    if (type) {
        *type = (MessageType)header.type;
    }
    if (status && payload && header.payload_len >= COMMAND_STATUS_SERIALIZED_SIZE) {
        protocol_deserialize_command_status(payload, status);
    }
    free(payload);
    return 0;
}

TEST(server_handle_command_clamps_speed_limits) {
    Server* server = server_create(0, 20, 20, 2);
    ASSERT_TRUE(server != NULL);

    NetSocket* socket = make_mock_socket(true, -1);
    ASSERT_TRUE(socket != NULL);
    ClientSession* client = server_add_client(server, socket);
    ASSERT_TRUE(client != NULL);

    server->speed_multiplier = 9.0f;
    server_handle_command(server, client, CMD_SPEED_UP, NULL);
    ASSERT_TRUE(server->speed_multiplier == 10.0f);

    server_handle_command(server, client, CMD_SPEED_UP, NULL);
    ASSERT_TRUE(server->speed_multiplier == 10.0f);

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

    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    NetSocket* socket = make_mock_socket(true, fds[0]);
    ASSERT_TRUE(socket != NULL);
    ClientSession* client = server_add_client(server, socket);
    ASSERT_TRUE(client != NULL);

    client->selected_colony = 77;

    World* old_world = server->world;
    AtomicWorld* old_atomic = server->atomic_world;
    ParallelContext* old_parallel = server->parallel_ctx;

    server_handle_command(server, client, CMD_RESET, NULL);

    MessageType status_type;
    ProtoCommandStatus status;
    ASSERT_EQ(read_command_status_message(fds[1], &status_type, &status), 0);
    ASSERT_EQ(status_type, MSG_ACK);
    ASSERT_EQ(status.command, (uint32_t)CMD_RESET);
    ASSERT_EQ(status.status_code, (uint32_t)PROTO_COMMAND_STATUS_ACCEPTED);
    ASSERT_EQ(status.entity_id, 0u);

    ASSERT_TRUE(server->world != NULL);
    ASSERT_TRUE(server->atomic_world != NULL);
    ASSERT_TRUE(server->parallel_ctx != NULL);
    ASSERT_NE(server->world, old_world);
    ASSERT_NE(server->atomic_world, old_atomic);
    ASSERT_EQ(server->parallel_ctx, old_parallel);
    ASSERT_EQ(server->world->width, 21);
    ASSERT_EQ(server->world->height, 13);
    ASSERT_EQ(client->selected_colony, 0u);

    server_destroy(server);
    close(fds[1]);
}

TEST(server_handle_command_data_branches_for_select_and_spawn) {
    Server* server = server_create(0, 20, 20, 2);
    ASSERT_TRUE(server != NULL);

    int fds[2] = {-1, -1};

    NetSocket* socket = make_mock_socket(true, -1);
    ASSERT_TRUE(socket != NULL);
    ClientSession* client = server_add_client(server, socket);
    ASSERT_TRUE(client != NULL);
    client->id = 77;

    server_handle_command(server, client, CMD_SELECT_COLONY, NULL);
    ASSERT_EQ(client->selected_colony, 0u);

    CommandSelectColony select_colony = {.colony_id = 42};
    server_handle_command(server, client, CMD_SELECT_COLONY, &select_colony);
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    client->socket->fd = fds[0];

    server_handle_command(server, client, CMD_SELECT_COLONY, &select_colony);
    MessageType last_status_type;
    ProtoCommandStatus last_status;
    ASSERT_EQ(read_command_status_message(fds[1], &last_status_type, &last_status), 0);
    ASSERT_EQ(last_status_type, MSG_ERROR);
    ASSERT_EQ(last_status.command, (uint32_t)CMD_SELECT_COLONY);
    ASSERT_EQ(last_status.status_code, (uint32_t)PROTO_COMMAND_STATUS_REJECTED);
    ASSERT_EQ(last_status.entity_id, 42u);
    ASSERT_EQ(client->selected_colony, 0u);

    CommandSpawnColony select_spawn = {.x = 2.0f, .y = 4.0f};
    server_handle_command(server, client, CMD_SPAWN_COLONY, &select_spawn);
    ASSERT_EQ(read_command_status_message(fds[1], &last_status_type, &last_status), 0);
    ASSERT_EQ(last_status_type, MSG_ACK);
    uint32_t spawned_colony_id = last_status.entity_id;
    ASSERT_NE(spawned_colony_id, 0u);

    select_colony.colony_id = spawned_colony_id;
    server_handle_command(server, client, CMD_SELECT_COLONY, &select_colony);
    ASSERT_EQ(read_command_status_message(fds[1], &last_status_type, &last_status), 0);
    ASSERT_EQ(last_status_type, MSG_ACK);
    ASSERT_EQ(last_status.command, (uint32_t)CMD_SELECT_COLONY);
    ASSERT_EQ(last_status.status_code, (uint32_t)PROTO_COMMAND_STATUS_ACCEPTED);
    ASSERT_EQ(last_status.entity_id, spawned_colony_id);
    ASSERT_EQ(client->selected_colony, spawned_colony_id);
    ASSERT_EQ(read_command_status_message(fds[1], &last_status_type, NULL), 0);
    ASSERT_EQ(last_status_type, MSG_COLONY_INFO);

    select_colony.colony_id = 0;
    server_handle_command(server, client, CMD_SELECT_COLONY, &select_colony);
    ASSERT_EQ(read_command_status_message(fds[1], &last_status_type, &last_status), 0);
    ASSERT_EQ(last_status_type, MSG_ACK);
    ASSERT_EQ(last_status.command, (uint32_t)CMD_SELECT_COLONY);
    ASSERT_EQ(last_status.status_code, (uint32_t)PROTO_COMMAND_STATUS_ACCEPTED);
    ASSERT_EQ(last_status.entity_id, 0u);
    ASSERT_EQ(client->selected_colony, 0u);

    server_handle_command(server, client, CMD_SPAWN_COLONY, NULL);

    CommandSpawnColony spawn = {.x = 6.0f, .y = 4.0f};
    server_handle_command(server, client, CMD_SPAWN_COLONY, &spawn);
    ASSERT_EQ(read_command_status_message(fds[1], &last_status_type, &last_status), 0);
    ASSERT_EQ(last_status_type, MSG_ACK);
    ASSERT_EQ(last_status.command, (uint32_t)CMD_SPAWN_COLONY);
    ASSERT_EQ(last_status.status_code, (uint32_t)PROTO_COMMAND_STATUS_ACCEPTED);

    Cell* spawned_cell = world_get_cell(server->world, 6, 4);
    ASSERT_TRUE(spawned_cell != NULL);
    ASSERT_NE(spawned_cell->colony_id, 0u);

    Colony* spawned = world_get_colony(server->world, spawned_cell->colony_id);
    ASSERT_TRUE(spawned != NULL);
    ASSERT_EQ(spawned->cell_count, 1u);
    ASSERT_EQ(spawned->max_cell_count, 1u);

    CommandSpawnColony blocked_spawn = {.x = 6.0f, .y = 4.0f};
    size_t colony_count_before_blocked = server->world->colony_count;
    server_handle_command(server, client, CMD_SPAWN_COLONY, &blocked_spawn);
    ASSERT_EQ(server->world->colony_count, colony_count_before_blocked);
    ASSERT_EQ(read_command_status_message(fds[1], &last_status_type, &last_status), 0);
    ASSERT_EQ(last_status_type, MSG_ERROR);
    ASSERT_EQ(last_status.status_code, (uint32_t)PROTO_COMMAND_STATUS_CONFLICT);

    CommandSpawnColony oob_spawn = {.x = -1.0f, .y = 4.0f};
    server_handle_command(server, client, CMD_SPAWN_COLONY, &oob_spawn);
    ASSERT_EQ(server->world->colony_count, colony_count_before_blocked);
    ASSERT_EQ(read_command_status_message(fds[1], &last_status_type, &last_status), 0);
    ASSERT_EQ(last_status_type, MSG_ERROR);
    ASSERT_EQ(last_status.status_code, (uint32_t)PROTO_COMMAND_STATUS_OUT_OF_BOUNDS);

    server_destroy(server);
    close(fds[1]);
}

TEST(server_remove_client_noop_when_target_missing) {
    Server* server = server_create(0, 20, 20, 2);
    ASSERT_TRUE(server != NULL);

    NetSocket* socket = make_mock_socket(true, -1);
    ASSERT_TRUE(socket != NULL);
    ClientSession* tracked = server_add_client(server, socket);
    ASSERT_TRUE(tracked != NULL);
    ASSERT_EQ(server->client_count, 1);

    ClientSession ghost = {0};
    server_remove_client(server, &ghost);
    ASSERT_EQ(server->client_count, 1);

    server_destroy(server);
}

TEST(server_process_clients_skips_non_connected_clients) {
    Server* server = server_create(0, 20, 20, 2);
    ASSERT_TRUE(server != NULL);

    NetSocket* inactive_socket = make_mock_socket(true, -1);
    NetSocket* disconnected_socket = make_mock_socket(false, -1);
    ASSERT_TRUE(inactive_socket != NULL);
    ASSERT_TRUE(disconnected_socket != NULL);

    ClientSession* inactive_client = server_add_client(server, inactive_socket);
    ClientSession* disconnected_client = server_add_client(server, disconnected_socket);
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

    server->running = false;
    server_stop(server);
    ASSERT_TRUE(!server->running);

    server->running = true;
    server_stop(server);
    ASSERT_TRUE(!server->running);

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
