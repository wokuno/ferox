#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/client/input.h"
#include "../src/client/renderer.h"
#include "../src/shared/network.h"
#include "../src/shared/protocol.h"

static int g_send_calls = 0;
static MessageType g_last_send_type = MSG_ERROR;
static ProtoAckPayload g_last_ack;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static int test_protocol_send_message(int socket, MessageType type, const uint8_t* payload, size_t len) {
    (void)socket;
    g_send_calls++;
    g_last_send_type = type;

    if (type == MSG_ACK) {
        CHECK(protocol_deserialize_ack(payload, len, &g_last_ack) == PROTO_ACK_SERIALIZED_SIZE);
    }

    return 0;
}

Renderer* renderer_create(void) { return (Renderer*)calloc(1, sizeof(Renderer)); }
void renderer_destroy(Renderer* renderer) { free(renderer); }
void renderer_clear(Renderer* renderer) { (void)renderer; }
void renderer_draw_world(Renderer* renderer, const ProtoWorld* world) { (void)renderer; (void)world; }
void renderer_draw_cell(Renderer* renderer, int x, int y, uint8_t r, uint8_t g, uint8_t b, bool is_border) {
    (void)renderer; (void)x; (void)y; (void)r; (void)g; (void)b; (void)is_border;
}
void renderer_draw_border(Renderer* renderer, int world_width, int world_height) {
    (void)renderer; (void)world_width; (void)world_height;
}
void renderer_draw_colony_info(Renderer* renderer, const ProtoColony* colony, const ProtoColonyDetail* detail) {
    (void)renderer; (void)colony; (void)detail;
}
void renderer_draw_status(Renderer* renderer, uint32_t tick, int colony_count, bool paused, float speed) {
    (void)renderer; (void)tick; (void)colony_count; (void)paused; (void)speed;
}
void renderer_present(Renderer* renderer) { (void)renderer; }
void renderer_scroll(Renderer* renderer, int dx, int dy) { (void)renderer; (void)dx; (void)dy; }
void renderer_center_on(Renderer* renderer, int x, int y) { (void)renderer; (void)x; (void)y; }
void renderer_get_terminal_size(int* width, int* height) {
    if (width) *width = 80;
    if (height) *height = 24;
}
void renderer_set_color_fg(Renderer* renderer, uint8_t r, uint8_t g, uint8_t b) {
    (void)renderer; (void)r; (void)g; (void)b;
}
void renderer_set_color_bg(Renderer* renderer, uint8_t r, uint8_t g, uint8_t b) {
    (void)renderer; (void)r; (void)g; (void)b;
}
void renderer_reset_colors(Renderer* renderer) { (void)renderer; }
void renderer_move_cursor(Renderer* renderer, int row, int col) { (void)renderer; (void)row; (void)col; }
void renderer_write(Renderer* renderer, const char* str) { (void)renderer; (void)str; }
void renderer_writef(Renderer* renderer, const char* fmt, ...) { (void)renderer; (void)fmt; }
int format_ansi_rgb_fg(char* buf, size_t size, uint8_t r, uint8_t g, uint8_t b) {
    (void)buf; (void)size; (void)r; (void)g; (void)b;
    return 0;
}
int format_ansi_rgb_bg(char* buf, size_t size, uint8_t r, uint8_t g, uint8_t b) {
    (void)buf; (void)size; (void)r; (void)g; (void)b;
    return 0;
}

NetSocket* net_client_connect(const char* host, uint16_t port) { (void)host; (void)port; return NULL; }
void net_socket_close(NetSocket* socket) { (void)socket; }
bool net_has_data(NetSocket* socket) { (void)socket; return false; }
void net_set_nonblocking(NetSocket* socket, bool nonblocking) { (void)socket; (void)nonblocking; }
void net_set_nodelay(NetSocket* socket, bool nodelay) { (void)socket; (void)nodelay; }

void input_init(void) {}
void input_cleanup(void) {}
InputAction input_poll(void) { return INPUT_NONE; }
int input_poll_char(void) { return -1; }
bool input_is_initialized(void) { return true; }

#define protocol_send_message test_protocol_send_message
#include "../src/client/client.c"
#undef protocol_send_message

static void test_chunked_world_parent_sequence_zero_is_acked(void) {
    Client client;
    memset(&client, 0, sizeof(client));

    NetSocket socket = {
        .fd = 42,
        .connected = true
    };
    client.socket = &socket;
    client.connected = true;
    protocol_ack_window_init(&client.world_ack_window);

    ProtoWorld world;
    proto_world_init(&world);
    world.width = 4;
    world.height = 2;
    world.tick = 77;
    world.speed_multiplier = 1.0f;

    uint8_t* world_payload = NULL;
    size_t world_len = 0;
    CHECK(protocol_serialize_world_state(&world, &world_payload, &world_len) == 0);

    client_handle_message_with_sequence(&client, MSG_WORLD_STATE, 0u, true, world_payload, world_len);
    CHECK(client.pending_grid_sequence == 0u);
    CHECK(client.pending_grid_sequence_valid);
    CHECK(g_send_calls == 0);

    uint16_t cells[8] = {1, 1, 0, 2, 2, 0, 3, 3};
    ProtoWorldDeltaGridChunk chunk = {
        .tick = 77,
        .width = 4,
        .height = 2,
        .total_cells = 8,
        .start_index = 0,
        .cell_count = 8,
        .final_chunk = true,
        .cells = cells
    };

    uint8_t* chunk_payload = NULL;
    size_t chunk_len = 0;
    CHECK(protocol_serialize_world_delta_grid_chunk(&chunk, &chunk_payload, &chunk_len) == 0);

    client_handle_message_with_sequence(&client, MSG_WORLD_DELTA, 1u, true, chunk_payload, chunk_len);
    CHECK(g_send_calls == 1);
    CHECK(g_last_send_type == MSG_ACK);
    CHECK(g_last_ack.channel == PROTO_ACK_CHANNEL_WORLD_UPDATE);
    CHECK(g_last_ack.latest_sequence == 0u);
    CHECK(g_last_ack.ack_bits == 0u);
    CHECK(!client.pending_grid_sequence_valid);
    CHECK(client.local_world.has_grid);

    free(world_payload);
    free(chunk_payload);
    proto_world_free(&client.local_world);
}

int main(void) {
    test_chunked_world_parent_sequence_zero_is_acked();
    return 0;
}
