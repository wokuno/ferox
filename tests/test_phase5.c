/**
 * test_phase5.c - Integration tests for Phase 5 Server Implementation
 * Part of Phase 5: Server Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
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

static int add_socketpair_client(Server* server, int fds[2], ClientSession** out_client) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return -1;
    }

    NetSocket* server_socket = (NetSocket*)calloc(1, sizeof(NetSocket));
    if (!server_socket) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    server_socket->fd = fds[1];
    server_socket->connected = true;
    net_set_nonblocking(server_socket, true);

    ClientSession* client = server_add_client(server, server_socket);
    if (!client) {
        free(server_socket);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    *out_client = client;
    return 0;
}

static int set_dummy_send_batch(ClientSendBatch* batch,
                                size_t frame_len,
                                bool started,
                                uint32_t sequence,
                                uint32_t tick) {
    if (!batch || frame_len == 0) {
        return -1;
    }

    ClientFrame* frame = (ClientFrame*)calloc(1, sizeof(ClientFrame));
    uint8_t* data = (uint8_t*)malloc(frame_len);
    if (!frame || !data) {
        free(frame);
        free(data);
        return -1;
    }
    memset(data, 0xA5, frame_len);
    frame->data = data;
    frame->len = frame_len;

    memset(batch, 0, sizeof(*batch));
    batch->frames = frame;
    batch->frame_count = 1;
    batch->frame_capacity = 1;
    batch->started = started;
    batch->has_world_sequence = true;
    batch->world_sequence = sequence;
    batch->has_world_tick = true;
    batch->world_tick = tick;
    return 0;
}

static bool fill_socket_send_buffer(int fd) {
    int sndbuf = 4096;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    uint8_t buffer[4096];
    memset(buffer, 0x5A, sizeof(buffer));
    for (int i = 0; i < 8192; i++) {
        ssize_t written = send(fd, buffer, sizeof(buffer), 0);
        if (written < 0) {
            return errno == EAGAIN || errno == EWOULDBLOCK;
        }
        if (written == 0) {
            return false;
        }
    }
    return false;
}

static uint32_t grid_cell_count(const World* world) {
    return (uint32_t)(world->width * world->height);
}

static void set_world_grid_pattern(World* world, uint32_t modulus) {
    uint32_t cells = grid_cell_count(world);
    for (uint32_t i = 0; i < cells; i++) {
        world->cells[i].colony_id = (modulus > 0 && i % modulus == 0) ? 1u : 0u;
    }
}

static void set_world_grid_noisy_pattern(World* world) {
    uint32_t cells = grid_cell_count(world);
    for (uint32_t i = 0; i < cells; i++) {
        world->cells[i].colony_id = (i % 251u) + 1u;
    }
}

static int install_acked_baseline(ClientSession* client, const World* world, uint32_t sequence, uint32_t tick) {
    ClientBaselineEntry* entry = &client->baseline_ring[0];
    uint32_t cells = grid_cell_count(world);
    entry->occupied = true;
    entry->sent = true;
    entry->acked = true;
    entry->sequence = sequence;
    entry->tick = tick;
    entry->width = (uint32_t)world->width;
    entry->height = (uint32_t)world->height;
    entry->grid_size = cells;
    entry->bytes = (size_t)cells * sizeof(uint16_t);
    entry->grid = (uint16_t*)calloc((size_t)cells, sizeof(uint16_t));
    if (!entry->grid) {
        return -1;
    }
    for (uint32_t i = 0; i < cells; i++) {
        entry->grid[i] = (uint16_t)world->cells[i].colony_id;
    }
    client->transport.has_last_completed_world = true;
    client->transport.last_completed_world_sequence = sequence;
    client->transport.last_completed_world_tick = tick;
    client->transport.has_last_acked_world = true;
    client->transport.last_acked_world_sequence = sequence;
    client->transport.last_acked_world_tick = tick;
    return 0;
}

static int drain_next_message(int fd, MessageHeader* header, uint8_t** payload) {
    fd_set readfds;
    struct timeval tv = {
        .tv_sec = 1,
        .tv_usec = 0
    };
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    if (select(fd + 1, &readfds, NULL, NULL, &tv) <= 0) {
        return -1;
    }
    return protocol_recv_message(fd, header, payload);
}

static int drain_world_update_frames(int fd,
                                     int max_messages,
                                     bool* saw_patch,
                                     bool* saw_chunk,
                                     ProtoWorldDeltaGridPatch* patch_out) {
    for (int i = 0; i < max_messages; i++) {
        MessageHeader header;
        uint8_t* payload = NULL;
        if (drain_next_message(fd, &header, &payload) < 0) {
            return -1;
        }

        if (header.type == MSG_WORLD_DELTA && payload && header.payload_len > 0) {
            if (payload[0] == (uint8_t)PROTO_WORLD_DELTA_GRID_PATCH) {
                *saw_patch = true;
                if (patch_out) {
                    TEST_ASSERT_EQ(protocol_deserialize_world_delta_grid_patch(payload,
                                                                               header.payload_len,
                                                                               patch_out),
                                   0, "Patch payload should deserialize");
                }
            } else if (payload[0] == (uint8_t)PROTO_WORLD_DELTA_GRID_CHUNK) {
                *saw_chunk = true;
            }
        }
        free(payload);
    }

    return 0;
}

// Test: Server creation with valid parameters
int test_server_creates_with_valid_parameters(void) {
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
int test_server_returns_null_for_invalid_params(void) {
    TEST_ASSERT(server_create(0, 0, 50, 2) == NULL, "Should fail with zero width");
    TEST_ASSERT(server_create(0, 50, 0, 2) == NULL, "Should fail with zero height");
    TEST_ASSERT(server_create(0, 50, 50, 0) == NULL, "Should fail with zero threads");
    TEST_ASSERT(server_create(0, -1, 50, 2) == NULL, "Should fail with negative width");
    
    return 0;
}

// Test: Server destruction (including NULL)
int test_server_destroy_handles_null_safely(void) {
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
int test_server_adds_and_removes_clients(void) {
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
int test_server_handles_pause_resume_commands(void) {
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

int test_server_process_clients_buffers_fragmented_command(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");

    int fds[2];
    TEST_ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0, "Socket pair should be created");

    NetSocket* server_socket = (NetSocket*)calloc(1, sizeof(NetSocket));
    TEST_ASSERT(server_socket != NULL, "Server socket wrapper should be allocated");
    server_socket->fd = fds[1];
    server_socket->connected = true;
    net_set_nonblocking(server_socket, true);

    ClientSession* client = server_add_client(server, server_socket);
    TEST_ASSERT(client != NULL, "Client should be added");

    uint8_t command_buffer[COMMAND_TYPE_SERIALIZED_SIZE];
    int command_len = protocol_serialize_command(CMD_PAUSE, NULL, command_buffer);
    TEST_ASSERT_EQ(command_len, COMMAND_TYPE_SERIALIZED_SIZE, "Pause command should serialize");

    uint8_t* frame = NULL;
    size_t frame_len = 0;
    TEST_ASSERT_EQ(protocol_build_message(MSG_COMMAND, command_buffer, (size_t)command_len, &frame, &frame_len),
                   0, "Command frame should be built");

    TEST_ASSERT_EQ(write(fds[0], frame, 5), 5, "Partial command frame should write");
    server_process_clients(server);
    TEST_ASSERT_EQ(server->paused, false, "Partial command should not be handled yet");
    TEST_ASSERT_EQ(client->active, true, "Client should remain active after partial frame");

    size_t remaining = frame_len - 5;
    TEST_ASSERT_EQ(write(fds[0], frame + 5, remaining), (ssize_t)remaining, "Remaining command frame should write");
    server_process_clients(server);
    TEST_ASSERT_EQ(server->paused, true, "Complete command should be handled");
    TEST_ASSERT_EQ(client->active, true, "Client should remain active after complete frame");

    free(frame);
    close(fds[0]);
    server_destroy(server);
    return 0;
}

int test_server_process_clients_records_world_ack(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");

    int fds[2];
    TEST_ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0, "Socket pair should be created");

    NetSocket* server_socket = (NetSocket*)calloc(1, sizeof(NetSocket));
    TEST_ASSERT(server_socket != NULL, "Server socket wrapper should be allocated");
    server_socket->fd = fds[1];
    server_socket->connected = true;
    net_set_nonblocking(server_socket, true);

    ClientSession* client = server_add_client(server, server_socket);
    TEST_ASSERT(client != NULL, "Client should be added");
    client->baseline_ring[0].occupied = true;
    client->baseline_ring[0].sent = true;
    client->baseline_ring[0].sequence = 101u;
    client->baseline_ring[0].tick = 9u;
    client->baseline_ring[1].occupied = true;
    client->baseline_ring[1].sent = true;
    client->baseline_ring[1].sequence = 100u;
    client->baseline_ring[1].tick = 8u;
    client->transport.has_last_queued_world = true;
    client->transport.last_queued_world_sequence = 102u;
    client->transport.last_queued_world_tick = 10u;

    ProtoAckPayload ack = {
        .channel = PROTO_ACK_CHANNEL_WORLD_UPDATE,
        .latest_sequence = 101u,
        .ack_bits = 0x1u
    };
    uint8_t ack_buffer[PROTO_ACK_SERIALIZED_SIZE];
    TEST_ASSERT_EQ(protocol_serialize_ack(&ack, ack_buffer), PROTO_ACK_SERIALIZED_SIZE,
                   "ACK payload should serialize");
    TEST_ASSERT_EQ(protocol_send_message(fds[0], MSG_ACK, ack_buffer, sizeof(ack_buffer)), 0,
                   "ACK frame should send");

    server_process_clients(server);
    TEST_ASSERT(protocol_ack_window_contains(&client->world_ack_window, 101u),
                "ACK window should contain latest sequence");
    TEST_ASSERT(protocol_ack_window_contains(&client->world_ack_window, 100u),
                "ACK window should contain bitfield sequence");
    TEST_ASSERT_EQ(client->baseline_ring[0].acked, true, "Latest baseline should be marked acked");
    TEST_ASSERT_EQ(client->baseline_ring[1].acked, true, "Bitfield baseline should be marked acked");
    TEST_ASSERT_EQ(client->transport.ack_messages_received, 1u, "ACK message should be counted");
    TEST_ASSERT_EQ(client->transport.baselines_acked, 2u, "Two baselines should be counted as acked");
    TEST_ASSERT(client->transport.has_last_acked_world, "Latest ACKed world should be recorded");
    TEST_ASSERT_EQ(client->transport.last_acked_world_sequence, 101u,
                   "Newest ACKed baseline sequence should be recorded");
    TEST_ASSERT_EQ(client->transport.last_acked_world_tick, 9u,
                   "Newest ACKed baseline tick should be recorded");
    TEST_ASSERT_EQ(client->transport.queued_to_acked_lag_ticks, 1u,
                   "Queued-to-ACKed freshness lag should be tracked by tick");

    close(fds[0]);
    server_destroy(server);
    return 0;
}

int test_server_broadcast_records_pending_drop_telemetry(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    world_init_random_colonies(server->world, 2);

    int fds[2];
    ClientSession* client = NULL;
    TEST_ASSERT_EQ(add_socketpair_client(server, fds, &client), 0, "Client socket pair should be added");
    TEST_ASSERT_EQ(set_dummy_send_batch(&client->send_current, 1, true, 10u, 1u), 0,
                   "Dummy started world batch should be installed");
    TEST_ASSERT_EQ(set_dummy_send_batch(&client->send_pending, 1, false, 11u, 1u), 0,
                   "Dummy pending world batch should be installed");
    TEST_ASSERT(fill_socket_send_buffer(client->socket->fd), "Client send buffer should fill");

    server_broadcast_world_state(server);

    TEST_ASSERT_EQ(client->transport.world_batches_queued, 1u, "New world batch should be counted");
    TEST_ASSERT_EQ(client->transport.world_batches_replaced, 1u, "Pending stale batch should be replaced");
    TEST_ASSERT_EQ(client->transport.unsent_world_batches_replaced, 0u,
                   "Started current batch should not be replaced");
    TEST_ASSERT_EQ(client->transport.pending_world_batches_replaced, 1u,
                   "Pending replacement counter should be incremented");
    TEST_ASSERT(client->transport.send_backpressure_events > 0u,
                "Backpressure should keep the started current batch active");
    TEST_ASSERT_EQ(client->transport.max_queue_depth_frames, 2u,
                   "Queue depth should remain bounded to current plus pending");
    TEST_ASSERT(client->send_pending.has_world_sequence, "Pending batch should hold newest world");
    TEST_ASSERT(client->send_pending.world_sequence == client->transport.last_queued_world_sequence,
                "Pending batch should match last queued sequence");

    close(fds[0]);
    server_destroy(server);
    return 0;
}

int test_server_broadcast_records_coalescing_telemetry(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    world_init_random_colonies(server->world, 2);

    int fds[2];
    ClientSession* client = NULL;
    TEST_ASSERT_EQ(add_socketpair_client(server, fds, &client), 0, "Client socket pair should be added");
    TEST_ASSERT_EQ(set_dummy_send_batch(&client->send_current, 1, false, 10u, 1u), 0,
                   "Dummy unsent world batch should be installed");

    server_broadcast_world_state(server);

    TEST_ASSERT_EQ(client->transport.world_batches_queued, 1u, "New world batch should be counted");
    TEST_ASSERT_EQ(client->transport.world_batches_replaced, 1u, "Unsent stale batch should be replaced");
    TEST_ASSERT_EQ(client->transport.unsent_world_batches_replaced, 1u,
                   "Unsent replacement counter should be incremented");
    TEST_ASSERT_EQ(client->transport.pending_world_batches_replaced, 0u,
                   "No pending batch should be replaced");
    TEST_ASSERT(client->transport.has_last_queued_world, "Last queued world should be recorded");
    TEST_ASSERT(client->transport.world_batches_started >= 1u, "Queued world should start sending");
    TEST_ASSERT(client->transport.world_batches_completed >= 1u, "Queued world should complete sending");
    TEST_ASSERT(client->transport.frames_sent > 0u, "At least one frame should be sent");
    TEST_ASSERT(client->transport.bytes_sent > 0u, "Sent bytes should be counted");
    TEST_ASSERT_EQ(client->transport.queue_depth_frames, 0u, "Queue should drain for healthy socket");

    close(fds[0]);
    server_destroy(server);
    return 0;
}

int test_server_pump_isolates_backpressured_client(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");

    int slow_fds[2];
    ClientSession* slow = NULL;
    TEST_ASSERT_EQ(add_socketpair_client(server, slow_fds, &slow), 0, "Slow client should be added");
    int healthy_fds[2];
    ClientSession* healthy = NULL;
    TEST_ASSERT_EQ(add_socketpair_client(server, healthy_fds, &healthy), 0, "Healthy client should be added");

    TEST_ASSERT(fill_socket_send_buffer(slow->socket->fd), "Slow client send buffer should fill");
    TEST_ASSERT_EQ(set_dummy_send_batch(&slow->send_current, 1, true, 20u, 2u), 0,
                   "Slow client batch should be installed");
    TEST_ASSERT_EQ(set_dummy_send_batch(&healthy->send_current, 1, true, 21u, 2u), 0,
                   "Healthy client batch should be installed");

    server_process_clients(server);

    TEST_ASSERT(slow->transport.send_backpressure_events > 0u,
                "Slow client should record send backpressure");
    TEST_ASSERT_EQ(slow->transport.frames_sent, 0u, "Slow client frame should remain queued");
    TEST_ASSERT_EQ(slow->transport.queue_depth_frames, 1u, "Slow client should retain one queued frame");
    TEST_ASSERT_EQ(healthy->transport.send_backpressure_events, 0u,
                   "Healthy client should not see backpressure");
    TEST_ASSERT_EQ(healthy->transport.frames_sent, 1u, "Healthy client frame should send");
    TEST_ASSERT_EQ(healthy->transport.bytes_sent, 1u, "Healthy client sent byte should be counted");
    TEST_ASSERT_EQ(healthy->transport.world_batches_completed, 1u,
                   "Healthy client world batch should complete");
    TEST_ASSERT_EQ(healthy->transport.queue_depth_frames, 0u, "Healthy client queue should drain");

    close(slow_fds[0]);
    close(healthy_fds[0]);
    server_destroy(server);
    return 0;
}

int test_server_reset_clears_transport_freshness(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");

    int fds[2];
    ClientSession* client = NULL;
    TEST_ASSERT_EQ(add_socketpair_client(server, fds, &client), 0, "Client socket pair should be added");
    TEST_ASSERT_EQ(set_dummy_send_batch(&client->send_current, 1, true, 31u, 99u), 0,
                   "Dummy current world batch should be installed");
    TEST_ASSERT_EQ(set_dummy_send_batch(&client->send_pending, 1, false, 32u, 100u), 0,
                   "Dummy pending world batch should be installed");
    client->baseline_ring[0].occupied = true;
    client->baseline_ring[0].sequence = 31u;
    client->baseline_ring[0].tick = 99u;
    client->transport.has_last_queued_world = true;
    client->transport.last_queued_world_tick = 100u;
    client->transport.has_last_started_world = true;
    client->transport.last_started_world_tick = 99u;
    client->transport.has_last_acked_world = true;
    client->transport.last_acked_world_tick = 98u;
    client->transport.queue_depth_frames = 2u;
    protocol_ack_window_record(&client->world_ack_window, 31u);

    server_handle_command(server, client, CMD_RESET, NULL);

    TEST_ASSERT(!client->send_current.frames, "Current batch should be cleared");
    TEST_ASSERT(!client->send_pending.frames, "Pending batch should be cleared");
    TEST_ASSERT_EQ(client->baseline_ring[0].occupied, false, "Baseline ring should be cleared");
    TEST_ASSERT_EQ(client->baseline_next, 0u, "Baseline insertion cursor should reset");
    TEST_ASSERT_EQ(client->world_ack_window.initialized, false, "ACK window should reset");
    TEST_ASSERT_EQ(client->transport.has_last_queued_world, false, "Queued freshness should reset");
    TEST_ASSERT_EQ(client->transport.has_last_started_world, false, "Started freshness should reset");
    TEST_ASSERT_EQ(client->transport.has_last_acked_world, false, "ACK freshness should reset");
    TEST_ASSERT_EQ(client->transport.queue_depth_frames, 0u, "Queue depth should reset");
    TEST_ASSERT_EQ(client->transport.queued_to_started_lag_ticks, 0u, "Started lag should reset");
    TEST_ASSERT_EQ(client->transport.queued_to_acked_lag_ticks, 0u, "ACK lag should reset");

    close(fds[0]);
    server_destroy(server);
    return 0;
}

int test_server_broadcast_uses_patch_for_acked_sparse_baseline(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    set_world_grid_noisy_pattern(server->world);

    int fds[2];
    ClientSession* client = NULL;
    TEST_ASSERT_EQ(add_socketpair_client(server, fds, &client), 0, "Client socket pair should be added");
    TEST_ASSERT_EQ(install_acked_baseline(client, server->world, 777u, server->world->tick),
                   0, "ACKed baseline should install");

    server->world->tick++;
    server->world->cells[3].colony_id = 400u;
    server->world->cells[123].colony_id = 401u;
    server_broadcast_world_state(server);

    bool saw_patch = false;
    bool saw_chunk = false;
    ProtoWorldDeltaGridPatch patch;
    proto_world_delta_grid_patch_init(&patch);
    TEST_ASSERT_EQ(drain_world_update_frames(fds[0], 2, &saw_patch, &saw_chunk, &patch),
                   0, "World state and patch should drain");
    TEST_ASSERT(saw_patch, "Sparse changed-cell patch should be sent");
    TEST_ASSERT_EQ(saw_chunk, false, "Sparse patch should not use grid chunk fallback");
    TEST_ASSERT_EQ(patch.base_sequence, 777u, "Patch should reference ACKed baseline sequence");
    TEST_ASSERT_EQ(patch.change_count, 2u, "Patch should contain only changed cells");
    TEST_ASSERT_EQ(patch.indices[0], 3u, "First changed index should be sorted");
    TEST_ASSERT_EQ(patch.cells[0], 400u, "First changed value should match");
    TEST_ASSERT_EQ(patch.indices[1], 123u, "Second changed index should be sorted");
    TEST_ASSERT_EQ(patch.cells[1], 401u, "Second changed value should match");

    proto_world_delta_grid_patch_free(&patch);
    close(fds[0]);
    server_destroy(server);
    return 0;
}

int test_server_broadcast_prefers_compressed_inline_over_patch(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    set_world_grid_pattern(server->world, 0);

    int fds[2];
    ClientSession* client = NULL;
    TEST_ASSERT_EQ(add_socketpair_client(server, fds, &client), 0, "Client socket pair should be added");
    TEST_ASSERT_EQ(install_acked_baseline(client, server->world, 778u, server->world->tick),
                   0, "ACKed baseline should install");

    server->world->tick++;
    server->world->cells[3].colony_id = 4u;
    server->world->cells[123].colony_id = 5u;
    server_broadcast_world_state(server);

    MessageHeader header;
    uint8_t* payload = NULL;
    TEST_ASSERT_EQ(drain_next_message(fds[0], &header, &payload), 0, "World state should drain");
    TEST_ASSERT_EQ(header.type, MSG_WORLD_STATE, "First frame should be world state");
    ProtoWorld world;
    proto_world_init(&world);
    TEST_ASSERT_EQ(protocol_deserialize_world_state(payload, header.payload_len, &world),
                   0, "World state should deserialize");
    TEST_ASSERT(world.has_grid, "Compressed inline grid should beat parent plus patch");

    proto_world_free(&world);
    free(payload);
    close(fds[0]);
    server_destroy(server);
    return 0;
}

int test_server_broadcast_falls_back_without_acked_baseline(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    set_world_grid_pattern(server->world, 17u);

    int fds[2];
    ClientSession* client = NULL;
    TEST_ASSERT_EQ(add_socketpair_client(server, fds, &client), 0, "Client socket pair should be added");

    server_broadcast_world_state(server);

    MessageHeader header;
    uint8_t* payload = NULL;
    TEST_ASSERT_EQ(drain_next_message(fds[0], &header, &payload), 0, "World state should drain");
    TEST_ASSERT_EQ(header.type, MSG_WORLD_STATE, "First frame should be world state");
    ProtoWorld world;
    proto_world_init(&world);
    TEST_ASSERT_EQ(protocol_deserialize_world_state(payload, header.payload_len, &world),
                   0, "World state should deserialize");
    TEST_ASSERT(world.has_grid, "No baseline should fall back to inline full grid");

    proto_world_free(&world);
    free(payload);
    close(fds[0]);
    server_destroy(server);
    return 0;
}

int test_server_broadcast_falls_back_when_patch_not_useful(void) {
    Server* server = server_create(0, 50, 50, 2);
    TEST_ASSERT(server != NULL, "Server should be created");
    set_world_grid_pattern(server->world, 0);

    int fds[2];
    ClientSession* client = NULL;
    TEST_ASSERT_EQ(add_socketpair_client(server, fds, &client), 0, "Client socket pair should be added");
    TEST_ASSERT_EQ(install_acked_baseline(client, server->world, 888u, server->world->tick),
                   0, "ACKed baseline should install");

    uint32_t cells = grid_cell_count(server->world);
    server->world->tick++;
    for (uint32_t i = 0; i < cells; i++) {
        server->world->cells[i].colony_id = (i % 251u) + 1u;
    }
    server_broadcast_world_state(server);

    MessageHeader header;
    uint8_t* payload = NULL;
    TEST_ASSERT_EQ(drain_next_message(fds[0], &header, &payload), 0, "World state should drain");
    TEST_ASSERT_EQ(header.type, MSG_WORLD_STATE, "First frame should be world state");
    ProtoWorld world;
    proto_world_init(&world);
    TEST_ASSERT_EQ(protocol_deserialize_world_state(payload, header.payload_len, &world),
                   0, "World state should deserialize");
    TEST_ASSERT(world.has_grid, "Dense patch should fall back to inline full grid");

    proto_world_free(&world);
    free(payload);
    close(fds[0]);
    server_destroy(server);
    return 0;
}

// Test: Server port assignment
int test_server_assigns_unique_ports(void) {
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
int test_server_initializes_world_with_colonies(void) {
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
int test_server_accepts_client_connection(void) {
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
int test_server_functions_handle_null_safely(void) {
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
int test_server_uses_default_tick_rate(void) {
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
    RUN_TEST(test_server_creates_with_valid_parameters);
    RUN_TEST(test_server_returns_null_for_invalid_params);
    RUN_TEST(test_server_destroy_handles_null_safely);
    RUN_TEST(test_server_assigns_unique_ports);
    
    // Client management tests
    printf("\n--- Client Management Tests ---\n");
    RUN_TEST(test_server_adds_and_removes_clients);
    
    // Command handling tests
    printf("\n--- Command Handling Tests ---\n");
    RUN_TEST(test_server_handles_pause_resume_commands);
    RUN_TEST(test_server_process_clients_buffers_fragmented_command);
    RUN_TEST(test_server_process_clients_records_world_ack);
    RUN_TEST(test_server_broadcast_records_pending_drop_telemetry);
    RUN_TEST(test_server_broadcast_records_coalescing_telemetry);
    RUN_TEST(test_server_pump_isolates_backpressured_client);
    RUN_TEST(test_server_reset_clears_transport_freshness);
    RUN_TEST(test_server_broadcast_uses_patch_for_acked_sparse_baseline);
    RUN_TEST(test_server_broadcast_prefers_compressed_inline_over_patch);
    RUN_TEST(test_server_broadcast_falls_back_without_acked_baseline);
    RUN_TEST(test_server_broadcast_falls_back_when_patch_not_useful);
    
    // World tests
    printf("\n--- World Tests ---\n");
    RUN_TEST(test_server_initializes_world_with_colonies);
    RUN_TEST(test_server_uses_default_tick_rate);
    
    // Integration tests
    printf("\n--- Integration Tests ---\n");
    RUN_TEST(test_server_accepts_client_connection);
    
    // Edge case tests
    printf("\n--- Edge Case Tests ---\n");
    RUN_TEST(test_server_functions_handle_null_safely);
    
    printf("\n=== Results ===\n");
    printf("Passed: %d/%d\n", passed, total);
    if (failed > 0) {
        printf("Failed: %d\n", failed);
        return 1;
    }
    
    printf("All tests passed!\n");
    return 0;
}
