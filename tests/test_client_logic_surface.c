#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/client/client.h"
#include "../src/client/input.h"

typedef struct {
    int create_calls;
    int destroy_calls;
    int center_calls;
    int scroll_calls;
    int clear_calls;
    int draw_border_calls;
    int draw_world_calls;
    int draw_colony_info_calls;
    int draw_status_calls;
    int present_calls;
    int last_center_x;
    int last_center_y;
    int last_scroll_dx;
    int last_scroll_dy;
} renderer_stub_state;

typedef struct {
    int connect_calls;
    int close_calls;
    int has_data_calls;
    bool has_data_return;
    net_socket* connect_result;
} network_stub_state;

typedef struct {
    int send_calls;
    int recv_calls;
    int serialize_calls;
    int deserialize_world_calls;
    int send_return;
    int recv_return;
    int serialize_return;
    int deserialize_world_return;
    MessageType last_send_type;
    CommandType last_serialized_cmd;
    uint32_t last_selected_colony_payload;
} protocol_stub_state;

static renderer_stub_state g_renderer;
static network_stub_state g_network;
static protocol_stub_state g_protocol;
static InputAction g_next_action = INPUT_NONE;
static int g_tests_run = 0;

static void reset_stubs(void) {
    memset(&g_renderer, 0, sizeof(g_renderer));
    memset(&g_network, 0, sizeof(g_network));
    memset(&g_protocol, 0, sizeof(g_protocol));
    g_protocol.send_return = 0;
    g_protocol.recv_return = 1;
    g_protocol.serialize_return = 1;
    g_protocol.deserialize_world_return = 0;
    g_next_action = INPUT_NONE;
}

Renderer* renderer_create(void) {
    g_renderer.create_calls++;
    Renderer* r = (Renderer*)calloc(1, sizeof(Renderer));
    assert(r != NULL);
    r->view_width = 40;
    r->view_height = 20;
    return r;
}

void renderer_destroy(Renderer* renderer) {
    g_renderer.destroy_calls++;
    free(renderer);
}

void renderer_center_on(Renderer* renderer, int x, int y) {
    g_renderer.center_calls++;
    g_renderer.last_center_x = x;
    g_renderer.last_center_y = y;
    renderer->view_x = x;
    renderer->view_y = y;
}

void renderer_scroll(Renderer* renderer, int dx, int dy) {
    g_renderer.scroll_calls++;
    g_renderer.last_scroll_dx = dx;
    g_renderer.last_scroll_dy = dy;
    renderer->view_x += dx;
    renderer->view_y += dy;
}

void renderer_clear(Renderer* renderer) { (void)renderer; g_renderer.clear_calls++; }
void renderer_draw_border(Renderer* renderer, int world_width, int world_height) {
    (void)renderer;
    (void)world_width;
    (void)world_height;
    g_renderer.draw_border_calls++;
}
void renderer_draw_world(Renderer* renderer, const proto_world* world) {
    (void)renderer;
    (void)world;
    g_renderer.draw_world_calls++;
}
void renderer_draw_colony_info(Renderer* renderer, const proto_colony* colony) {
    (void)renderer;
    (void)colony;
    g_renderer.draw_colony_info_calls++;
}
void renderer_draw_status(Renderer* renderer, uint32_t tick, int colony_count, bool paused, float speed) {
    (void)renderer;
    (void)tick;
    (void)colony_count;
    (void)paused;
    (void)speed;
    g_renderer.draw_status_calls++;
}
void renderer_present(Renderer* renderer) { (void)renderer; g_renderer.present_calls++; }

net_socket* net_client_connect(const char* host, uint16_t port) {
    (void)host;
    (void)port;
    g_network.connect_calls++;
    return g_network.connect_result;
}

void net_socket_close(net_socket* socket) {
    (void)socket;
    g_network.close_calls++;
}

bool net_has_data(net_socket* socket) {
    (void)socket;
    g_network.has_data_calls++;
    return g_network.has_data_return;
}

void net_set_nonblocking(net_socket* socket, bool nonblocking) { (void)socket; (void)nonblocking; }
void net_set_nodelay(net_socket* socket, bool nodelay) { (void)socket; (void)nodelay; }

int protocol_send_message(int socket, MessageType type, const uint8_t* payload, size_t len) {
    (void)socket;
    (void)len;
    g_protocol.send_calls++;
    g_protocol.last_send_type = type;
    if (type == MSG_COMMAND && payload != NULL && len >= sizeof(uint32_t) * 2) {
        uint32_t colony_id = 0;
        memcpy(&colony_id, payload + sizeof(uint32_t), sizeof(colony_id));
        g_protocol.last_selected_colony_payload = colony_id;
    }
    return g_protocol.send_return;
}

int protocol_recv_message(int socket, MessageHeader* header, uint8_t** payload) {
    (void)socket;
    (void)header;
    (void)payload;
    g_protocol.recv_calls++;
    return g_protocol.recv_return;
}

int protocol_serialize_command(CommandType cmd, const void* data, uint8_t* buffer) {
    g_protocol.serialize_calls++;
    g_protocol.last_serialized_cmd = cmd;
    if (g_protocol.serialize_return < 0) {
        return g_protocol.serialize_return;
    }

    if (buffer != NULL) {
        uint32_t raw_cmd = (uint32_t)cmd;
        memcpy(buffer, &raw_cmd, sizeof(raw_cmd));
        if (cmd == CMD_SELECT_COLONY && data != NULL) {
            const CommandSelectColony* select = (const CommandSelectColony*)data;
            memcpy(buffer + sizeof(raw_cmd), &select->colony_id, sizeof(select->colony_id));
            return (int)(sizeof(raw_cmd) + sizeof(select->colony_id));
        }
    }

    return (int)sizeof(uint32_t);
}

int protocol_deserialize_world_state(const uint8_t* buffer, size_t len, proto_world* world) {
    (void)buffer;
    (void)len;
    g_protocol.deserialize_world_calls++;
    if (g_protocol.deserialize_world_return == 0) {
        world->tick = 777;
    }
    return g_protocol.deserialize_world_return;
}

void proto_world_init(proto_world* world) {
    memset(world, 0, sizeof(*world));
    world->speed_multiplier = 1.0f;
}

InputAction input_poll(void) {
    return g_next_action;
}

#include "../src/client/client.c"

static Client make_client_with_renderer(void) {
    Client client;
    memset(&client, 0, sizeof(client));
    client.renderer = renderer_create();
    client.local_world.speed_multiplier = 1.0f;
    return client;
}

static void test_select_next_colony_branches(void) {
    g_tests_run++;
    reset_stubs();
    Client client = make_client_with_renderer();

    client.local_world.colony_count = 3;
    client.local_world.colonies[0].id = 101;
    client.local_world.colonies[0].alive = true;
    client.local_world.colonies[0].x = 12;
    client.local_world.colonies[0].y = 18;
    client.local_world.colonies[1].id = 202;
    client.local_world.colonies[1].alive = false;
    client.local_world.colonies[2].id = 303;
    client.local_world.colonies[2].alive = true;
    client.local_world.colonies[2].x = 44;
    client.local_world.colonies[2].y = 55;

    client.selected_colony = 0;
    client.selected_index = 2;
    client_select_next_colony(&client);
    assert(client.selected_colony == 101);
    assert(client.selected_index == 0);
    assert(g_renderer.center_calls == 1);

    client_select_next_colony(&client);
    assert(client.selected_colony == 303);
    assert(client.selected_index == 2);

    client.local_world.colonies[0].alive = false;
    client.local_world.colonies[2].alive = false;
    client_select_next_colony(&client);
    assert(client.selected_colony == 0);

    renderer_destroy(client.renderer);
}

static void test_select_next_colony_empty_world_resets(void) {
    g_tests_run++;
    reset_stubs();
    Client client = make_client_with_renderer();
    client.selected_colony = 999;
    client.selected_index = 12;

    client_select_next_colony(&client);
    assert(client.selected_colony == 0);
    assert(client.selected_index == 0);

    renderer_destroy(client.renderer);
}

static void test_get_selected_colony_filters_dead_and_missing(void) {
    g_tests_run++;
    reset_stubs();
    Client client;
    memset(&client, 0, sizeof(client));
    client.local_world.colony_count = 2;
    client.local_world.colonies[0].id = 1;
    client.local_world.colonies[0].alive = false;
    client.local_world.colonies[1].id = 2;
    client.local_world.colonies[1].alive = true;

    client.selected_colony = 0;
    assert(client_get_selected_colony(&client) == NULL);

    client.selected_colony = 99;
    assert(client_get_selected_colony(&client) == NULL);

    client.selected_colony = 1;
    assert(client_get_selected_colony(&client) == NULL);

    client.selected_colony = 2;
    assert(client_get_selected_colony(&client) == &client.local_world.colonies[1]);
}

static void test_handle_message_dispatches_world_messages(void) {
    g_tests_run++;
    reset_stubs();
    Client client;
    memset(&client, 0, sizeof(client));
    uint8_t payload[4] = {1, 2, 3, 4};

    client_handle_message(&client, MSG_WORLD_STATE, payload, sizeof(payload));
    client_handle_message(&client, MSG_WORLD_DELTA, payload, sizeof(payload));
    client_handle_message(&client, MSG_ACK, payload, sizeof(payload));
    client_handle_message(&client, MSG_ERROR, payload, sizeof(payload));
    client_handle_message(&client, (MessageType)999, payload, sizeof(payload));

    assert(g_protocol.deserialize_world_calls == 2);
}

static void test_update_world_guards_and_failures(void) {
    g_tests_run++;
    reset_stubs();
    Client client;
    memset(&client, 0, sizeof(client));
    uint8_t payload[4] = {0};

    client_update_world(NULL, payload, sizeof(payload));
    client_update_world(&client, NULL, sizeof(payload));
    assert(g_protocol.deserialize_world_calls == 0);

    g_protocol.deserialize_world_return = -1;
    client.local_world.tick = 12;
    client_update_world(&client, payload, sizeof(payload));
    assert(g_protocol.deserialize_world_calls == 1);
    assert(client.local_world.tick == 12);

    g_protocol.deserialize_world_return = 0;
    client_update_world(&client, payload, sizeof(payload));
    assert(g_protocol.deserialize_world_calls == 2);
    assert(client.local_world.tick == 777);
}

static void test_process_input_pause_speed_scroll_select_reset(void) {
    g_tests_run++;
    reset_stubs();
    Client client = make_client_with_renderer();
    net_socket socket = {.fd = 5};
    client.connected = true;
    client.socket = &socket;

    g_next_action = INPUT_PAUSE;
    client.local_world.paused = false;
    client_process_input(&client);
    assert(client.local_world.paused == true);
    assert(g_protocol.last_serialized_cmd == CMD_PAUSE);

    g_next_action = INPUT_PAUSE;
    client_process_input(&client);
    assert(client.local_world.paused == false);
    assert(g_protocol.last_serialized_cmd == CMD_RESUME);

    g_next_action = INPUT_SPEED_UP;
    client.local_world.speed_multiplier = 90.0f;
    client_process_input(&client);
    assert(client.local_world.speed_multiplier == 100.0f);
    assert(g_protocol.last_serialized_cmd == CMD_SPEED_UP);

    g_next_action = INPUT_SLOW_DOWN;
    client.local_world.speed_multiplier = 0.11f;
    client_process_input(&client);
    assert(client.local_world.speed_multiplier == 0.1f);
    assert(g_protocol.last_serialized_cmd == CMD_SLOW_DOWN);

    g_next_action = INPUT_SCROLL_LEFT;
    client_process_input(&client);
    assert(g_renderer.last_scroll_dx == -5);
    assert(g_renderer.last_scroll_dy == 0);

    g_next_action = INPUT_SCROLL_UP;
    client_process_input(&client);
    assert(g_renderer.last_scroll_dx == 0);
    assert(g_renderer.last_scroll_dy == -5);

    client.local_world.colony_count = 1;
    client.local_world.colonies[0].id = 42;
    client.local_world.colonies[0].alive = true;
    g_next_action = INPUT_SELECT;
    client_process_input(&client);
    assert(client.selected_colony == 42);
    assert(g_protocol.last_serialized_cmd == CMD_SELECT_COLONY);
    assert(g_protocol.last_selected_colony_payload == 42);

    g_next_action = INPUT_DESELECT;
    client_process_input(&client);
    assert(client.selected_colony == 0);

    g_next_action = INPUT_RESET;
    client_process_input(&client);
    assert(g_protocol.last_serialized_cmd == CMD_RESET);

    renderer_destroy(client.renderer);
}

static void test_process_input_quit_sets_running_false(void) {
    g_tests_run++;
    reset_stubs();
    Client client;
    memset(&client, 0, sizeof(client));
    client.running = true;

    g_next_action = INPUT_QUIT;
    client_process_input(&client);
    assert(client.running == false);
}

int main(void) {
    test_select_next_colony_branches();
    test_select_next_colony_empty_world_resets();
    test_get_selected_colony_filters_dead_and_missing();
    test_handle_message_dispatches_world_messages();
    test_update_world_guards_and_failures();
    test_process_input_pause_speed_scroll_select_reset();
    test_process_input_quit_sets_running_false();

    printf("test_client_logic_surface: %d tests passed\n", g_tests_run);
    return 0;
}
