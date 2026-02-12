#include "client.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define SCROLL_SPEED 5
#define FRAME_DELAY_US 33333  // ~30 FPS

Client* client_create(void) {
    Client* client = (Client*)malloc(sizeof(Client));
    if (!client) return NULL;
    
    memset(client, 0, sizeof(Client));
    
    client->renderer = renderer_create();
    if (!client->renderer) {
        free(client);
        return NULL;
    }
    
    client->socket = NULL;
    client->connected = false;
    client->running = false;
    client->selected_colony = 0;
    client->selected_index = 0;
    
    // Initialize local world
    memset(&client->local_world, 0, sizeof(ProtoWorld));
    client->local_world.speed_multiplier = 1.0f;
    
    return client;
}

void client_destroy(Client* client) {
    if (!client) return;
    
    if (client->connected) {
        client_disconnect(client);
    }
    
    if (client->renderer) {
        renderer_destroy(client->renderer);
    }
    
    free(client);
}

bool client_connect(Client* client, const char* host, uint16_t port) {
    if (!client || !host) return false;
    
    client->socket = net_client_connect(host, port);
    if (!client->socket) {
        return false;
    }
    
    // Set non-blocking for async reads
    net_set_nonblocking(client->socket, true);
    net_set_nodelay(client->socket, true);
    
    // Send connect message
    if (protocol_send_message(client->socket->fd, MSG_CONNECT, NULL, 0) < 0) {
        net_socket_close(client->socket);
        client->socket = NULL;
        return false;
    }
    
    client->connected = true;
    return true;
}

void client_disconnect(Client* client) {
    if (!client || !client->connected) return;
    
    // Send disconnect message (best effort)
    if (client->socket) {
        protocol_send_message(client->socket->fd, MSG_DISCONNECT, NULL, 0);
        net_socket_close(client->socket);
        client->socket = NULL;
    }
    
    client->connected = false;
}

void client_send_command(Client* client, CommandType cmd, void* data) {
    if (!client || !client->connected || !client->socket) return;
    
    uint8_t buffer[256];
    int len = protocol_serialize_command(cmd, data, buffer);
    if (len < 0) return;
    
    protocol_send_message(client->socket->fd, MSG_COMMAND, buffer, (size_t)len);
}

void client_handle_message(Client* client, MessageType type, const uint8_t* payload, size_t len) {
    if (!client) return;
    
    switch (type) {
        case MSG_WORLD_STATE:
            client_update_world(client, payload, len);
            break;
            
        case MSG_WORLD_DELTA:
            // For now, treat deltas like full world state
            client_update_world(client, payload, len);
            break;
            
        case MSG_COLONY_INFO:
            // Could update specific colony info
            break;
            
        case MSG_ACK:
            // Command acknowledged
            break;
            
        case MSG_ERROR:
            // Handle error
            break;
            
        default:
            break;
    }
}

void client_update_world(Client* client, const uint8_t* data, size_t len) {
    if (!client || !data) return;
    
    if (protocol_deserialize_world_state(data, len, &client->local_world) < 0) {
        // Failed to deserialize
        return;
    }
}

void client_select_next_colony(Client* client) {
    if (!client) return;
    
    if (client->local_world.colony_count == 0) {
        client->selected_colony = 0;
        client->selected_index = 0;
        return;
    }
    
    // Find next alive colony starting from current index
    uint32_t start_index = client->selected_index;
    uint32_t count = client->local_world.colony_count;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t check_index = (start_index + i + 1) % count;
        
        // If we've come full circle and nothing is selected, try starting from 0
        if (i == 0 && client->selected_colony == 0) {
            check_index = 0;
        }
        
        if (client->local_world.colonies[check_index].alive) {
            client->selected_index = check_index;
            client->selected_colony = client->local_world.colonies[check_index].id;
            
            // Center view on selected colony
            const ProtoColony* colony = &client->local_world.colonies[check_index];
            renderer_center_on(client->renderer, (int)colony->x, (int)colony->y);
            return;
        }
    }
    
    // No alive colonies
    client->selected_colony = 0;
}

void client_deselect_colony(Client* client) {
    if (!client) return;
    client->selected_colony = 0;
}

const ProtoColony* client_get_selected_colony(Client* client) {
    if (!client || client->selected_colony == 0) return NULL;
    
    for (uint32_t i = 0; i < client->local_world.colony_count; i++) {
        if (client->local_world.colonies[i].id == client->selected_colony) {
            // Return NULL for dead colonies (hide them from info panel)
            if (!client->local_world.colonies[i].alive) {
                return NULL;
            }
            return &client->local_world.colonies[i];
        }
    }
    
    return NULL;
}

static void client_process_input(Client* client) {
    InputAction action = input_poll();
    
    switch (action) {
        case INPUT_QUIT:
            client->running = false;
            break;
            
        case INPUT_PAUSE:
            if (client->local_world.paused) {
                client_send_command(client, CMD_RESUME, NULL);
            } else {
                client_send_command(client, CMD_PAUSE, NULL);
            }
            // Optimistically update local state
            client->local_world.paused = !client->local_world.paused;
            break;
            
        case INPUT_SPEED_UP:
            client_send_command(client, CMD_SPEED_UP, NULL);
            client->local_world.speed_multiplier *= 1.5f;
            if (client->local_world.speed_multiplier > 10.0f) {
                client->local_world.speed_multiplier = 10.0f;
            }
            break;
            
        case INPUT_SLOW_DOWN:
            client_send_command(client, CMD_SLOW_DOWN, NULL);
            client->local_world.speed_multiplier /= 1.5f;
            if (client->local_world.speed_multiplier < 0.1f) {
                client->local_world.speed_multiplier = 0.1f;
            }
            break;
            
        case INPUT_SCROLL_UP:
            renderer_scroll(client->renderer, 0, -SCROLL_SPEED);
            break;
            
        case INPUT_SCROLL_DOWN:
            renderer_scroll(client->renderer, 0, SCROLL_SPEED);
            break;
            
        case INPUT_SCROLL_LEFT:
            renderer_scroll(client->renderer, -SCROLL_SPEED, 0);
            break;
            
        case INPUT_SCROLL_RIGHT:
            renderer_scroll(client->renderer, SCROLL_SPEED, 0);
            break;
            
        case INPUT_SELECT:
            client_select_next_colony(client);
            if (client->selected_colony > 0) {
                CommandSelectColony cmd = { .colony_id = client->selected_colony };
                client_send_command(client, CMD_SELECT_COLONY, &cmd);
            }
            break;
            
        case INPUT_DESELECT:
            client_deselect_colony(client);
            break;
            
        case INPUT_RESET:
            client_send_command(client, CMD_RESET, NULL);
            break;
            
        case INPUT_NONE:
        default:
            break;
    }
}

static void client_receive_updates(Client* client) {
    if (!client->socket || !net_has_data(client->socket)) return;
    
    MessageHeader header;
    uint8_t* payload = NULL;
    
    // Set blocking temporarily for receive
    net_set_nonblocking(client->socket, false);
    
    if (protocol_recv_message(client->socket->fd, &header, &payload) == 0) {
        client_handle_message(client, (MessageType)header.type, payload, header.payload_len);
        free(payload);
    } else {
        // Connection lost
        client->connected = false;
        client->running = false;
    }
    
    net_set_nonblocking(client->socket, true);
}

static void client_render(Client* client) {
    renderer_clear(client->renderer);
    
    // Update renderer selection
    client->renderer->selected_colony = client->selected_colony;
    
    // Draw petri dish border
    renderer_draw_border(client->renderer, 
                         (int)client->local_world.width, 
                         (int)client->local_world.height);
    
    // Draw world
    renderer_draw_world(client->renderer, &client->local_world);
    
    // Draw colony info panel
    const ProtoColony* selected = client_get_selected_colony(client);
    renderer_draw_colony_info(client->renderer, selected);
    
    // Draw status bar
    int alive_count = 0;
    for (uint32_t i = 0; i < client->local_world.colony_count; i++) {
        if (client->local_world.colonies[i].alive) alive_count++;
    }
    
    renderer_draw_status(client->renderer,
                         client->local_world.tick,
                         alive_count,
                         client->local_world.paused,
                         client->local_world.speed_multiplier);
    
    renderer_present(client->renderer);
}

void client_run(Client* client) {
    if (!client) return;
    
    client->running = true;
    
    while (client->running) {
        // Process input
        client_process_input(client);
        
        // Receive network updates
        if (client->connected) {
            client_receive_updates(client);
        }
        
        // Render
        client_render(client);
        
        // Frame rate limiting
        usleep(FRAME_DELAY_US);
    }
}
