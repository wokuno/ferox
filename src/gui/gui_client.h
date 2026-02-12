#ifndef GUI_CLIENT_H
#define GUI_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "../shared/network.h"
#include "../shared/protocol.h"
#include "gui_renderer.h"

typedef struct GuiClient {
    NetSocket* socket;
    GuiRenderer* renderer;
    ProtoWorld local_world;       // Local copy of world state
    bool connected;
    bool running;
    uint32_t selected_colony;
    uint32_t selected_index;      // Index in colony array for cycling
    float fps;                    // Current frames per second
} GuiClient;

// Create and destroy
GuiClient* gui_client_create(void);
void gui_client_destroy(GuiClient* client);

// Connection management
bool gui_client_connect(GuiClient* client, const char* host, uint16_t port);
void gui_client_disconnect(GuiClient* client);

// Main loop
void gui_client_run(GuiClient* client);

// Commands
void gui_client_send_command(GuiClient* client, CommandType cmd, void* data);

// Message handling
void gui_client_handle_message(GuiClient* client, MessageType type, 
                                const uint8_t* payload, size_t len);
void gui_client_update_world(GuiClient* client, const uint8_t* data, size_t len);

// Selection
void gui_client_select_next_colony(GuiClient* client);
void gui_client_select_prev_colony(GuiClient* client);
void gui_client_deselect_colony(GuiClient* client);
void gui_client_select_colony_at(GuiClient* client, float world_x, float world_y);
const ProtoColony* gui_client_get_selected_colony(GuiClient* client);

#endif // GUI_CLIENT_H
