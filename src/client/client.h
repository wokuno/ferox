#ifndef CLIENT_H
#define CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "../shared/network.h"
#include "../shared/protocol.h"
#include "renderer.h"

typedef struct Client {
    net_socket* socket;
    Renderer* renderer;
    proto_world local_world;       // Local copy of world state
    bool connected;
    bool running;
    uint32_t selected_colony;
    uint32_t selected_index;  // Index in colony array for cycling
} Client;

// Create and destroy
Client* client_create(void);
void client_destroy(Client* client);

// Connection management
bool client_connect(Client* client, const char* host, uint16_t port);
void client_disconnect(Client* client);

// Main loop
void client_run(Client* client);  // Main loop

// Commands
void client_send_command(Client* client, CommandType cmd, void* data);

// Message handling
void client_handle_message(Client* client, MessageType type, const uint8_t* payload, size_t len);
void client_update_world(Client* client, const uint8_t* data, size_t len);

// Selection
void client_select_next_colony(Client* client);
void client_deselect_colony(Client* client);
const proto_colony* client_get_selected_colony(Client* client);

#endif // CLIENT_H
