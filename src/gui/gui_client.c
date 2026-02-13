#include "gui_client.h"
#include "gui_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#define usleep(x) Sleep((x) / 1000)
#else
#include <unistd.h>
#endif

#define FRAME_DELAY_US 16666  // ~60 FPS
#define PAN_SPEED 5.0f

GuiClient* gui_client_create(void) {
    GuiClient* client = (GuiClient*)calloc(1, sizeof(GuiClient));
    if (!client) return NULL;
    
    client->renderer = gui_renderer_create("Ferox - Bacterial Colony Simulator");
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
    proto_world_init(&client->local_world);
    client->local_world.speed_multiplier = 1.0f;
    client->local_world.width = 100;
    client->local_world.height = 100;
    
    return client;
}

void gui_client_destroy(GuiClient* client) {
    if (!client) return;
    
    if (client->connected) {
        gui_client_disconnect(client);
    }
    
    if (client->renderer) {
        gui_renderer_destroy(client->renderer);
    }
    
    proto_world_free(&client->local_world);
    
    free(client);
}

bool gui_client_connect(GuiClient* client, const char* host, uint16_t port) {
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

void gui_client_disconnect(GuiClient* client) {
    if (!client || !client->connected) return;
    
    if (client->socket) {
        protocol_send_message(client->socket->fd, MSG_DISCONNECT, NULL, 0);
        net_socket_close(client->socket);
        client->socket = NULL;
    }
    
    client->connected = false;
}

void gui_client_send_command(GuiClient* client, CommandType cmd, void* data) {
    if (!client || !client->connected || !client->socket) return;
    
    uint8_t buffer[256];
    int len = protocol_serialize_command(cmd, data, buffer);
    if (len < 0) return;
    
    protocol_send_message(client->socket->fd, MSG_COMMAND, buffer, (size_t)len);
}

void gui_client_handle_message(GuiClient* client, MessageType type,
                                const uint8_t* payload, size_t len) {
    if (!client) return;
    
    switch (type) {
        case MSG_WORLD_STATE:
        case MSG_WORLD_DELTA:
            gui_client_update_world(client, payload, len);
            break;
        case MSG_COLONY_INFO:
        case MSG_ACK:
        case MSG_ERROR:
        default:
            break;
    }
}

void gui_client_update_world(GuiClient* client, const uint8_t* data, size_t len) {
    if (!client || !data) return;
    
    // Free old grid before deserializing new one
    proto_world_free(&client->local_world);
    
    if (protocol_deserialize_world_state(data, len, &client->local_world) < 0) {
        return;
    }
}

void gui_client_select_next_colony(GuiClient* client) {
    if (!client || client->local_world.colony_count == 0) {
        client->selected_colony = 0;
        client->selected_index = 0;
        return;
    }
    
    uint32_t start_index = client->selected_index;
    uint32_t count = client->local_world.colony_count;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t check_index = (start_index + i + 1) % count;
        
        if (i == 0 && client->selected_colony == 0) {
            check_index = 0;
        }
        
        if (client->local_world.colonies[check_index].alive) {
            client->selected_index = check_index;
            client->selected_colony = client->local_world.colonies[check_index].id;
            
            const ProtoColony* colony = &client->local_world.colonies[check_index];
            gui_renderer_center_on(client->renderer, colony->x, colony->y);
            client->renderer->selected_colony = client->selected_colony;
            return;
        }
    }
    
    client->selected_colony = 0;
}

void gui_client_select_prev_colony(GuiClient* client) {
    if (!client || client->local_world.colony_count == 0) {
        client->selected_colony = 0;
        client->selected_index = 0;
        return;
    }
    
    uint32_t start_index = client->selected_index;
    uint32_t count = client->local_world.colony_count;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t check_index = (start_index + count - i - 1) % count;
        
        if (client->local_world.colonies[check_index].alive) {
            client->selected_index = check_index;
            client->selected_colony = client->local_world.colonies[check_index].id;
            
            const ProtoColony* colony = &client->local_world.colonies[check_index];
            gui_renderer_center_on(client->renderer, colony->x, colony->y);
            client->renderer->selected_colony = client->selected_colony;
            return;
        }
    }
    
    client->selected_colony = 0;
}

void gui_client_deselect_colony(GuiClient* client) {
    if (!client) return;
    client->selected_colony = 0;
    if (client->renderer)
        client->renderer->selected_colony = 0;
}

void gui_client_select_colony_at(GuiClient* client, float world_x, float world_y) {
    if (!client) return;
    
    // Find colony under cursor
    for (uint32_t i = 0; i < client->local_world.colony_count; i++) {
        const ProtoColony* colony = &client->local_world.colonies[i];
        if (!colony->alive) continue;
        
        float dx = world_x - colony->x;
        float dy = world_y - colony->y;
        float dist = sqrtf(dx * dx + dy * dy);
        
        // Check if within colony radius (with some tolerance)
        if (dist <= colony->radius * 1.5f) {
            client->selected_colony = colony->id;
            client->selected_index = i;
            client->renderer->selected_colony = colony->id;
            
            CommandSelectColony cmd = { .colony_id = colony->id };
            gui_client_send_command(client, CMD_SELECT_COLONY, &cmd);
            return;
        }
    }
    
    // No colony found - deselect
    gui_client_deselect_colony(client);
}

const ProtoColony* gui_client_get_selected_colony(GuiClient* client) {
    if (!client || client->selected_colony == 0) return NULL;
    
    for (uint32_t i = 0; i < client->local_world.colony_count; i++) {
        if (client->local_world.colonies[i].id == client->selected_colony) {
            if (!client->local_world.colonies[i].alive) return NULL;
            return &client->local_world.colonies[i];
        }
    }
    
    return NULL;
}

static void gui_client_process_input(GuiClient* client, GuiInputState* input) {
    switch (input->action) {
        case GUI_INPUT_QUIT:
            client->running = false;
            break;
            
        case GUI_INPUT_PAUSE:
            if (client->local_world.paused) {
                gui_client_send_command(client, CMD_RESUME, NULL);
            } else {
                gui_client_send_command(client, CMD_PAUSE, NULL);
            }
            client->local_world.paused = !client->local_world.paused;
            break;
            
        case GUI_INPUT_SPEED_UP:
            gui_client_send_command(client, CMD_SPEED_UP, NULL);
            client->local_world.speed_multiplier *= 1.5f;
            if (client->local_world.speed_multiplier > 100.0f) {
                client->local_world.speed_multiplier = 100.0f;
            }
            break;
            
        case GUI_INPUT_SLOW_DOWN:
            gui_client_send_command(client, CMD_SLOW_DOWN, NULL);
            client->local_world.speed_multiplier /= 1.5f;
            if (client->local_world.speed_multiplier < 0.1f) {
                client->local_world.speed_multiplier = 0.1f;
            }
            break;
            
        case GUI_INPUT_SELECT_NEXT:
            gui_client_select_next_colony(client);
            if (client->selected_colony > 0) {
                CommandSelectColony cmd = { .colony_id = client->selected_colony };
                gui_client_send_command(client, CMD_SELECT_COLONY, &cmd);
            }
            break;
            
        case GUI_INPUT_SELECT_PREV:
            gui_client_select_prev_colony(client);
            if (client->selected_colony > 0) {
                CommandSelectColony cmd = { .colony_id = client->selected_colony };
                gui_client_send_command(client, CMD_SELECT_COLONY, &cmd);
            }
            break;
            
        case GUI_INPUT_DESELECT:
            gui_client_deselect_colony(client);
            break;
            
        case GUI_INPUT_RESET:
            gui_client_send_command(client, CMD_RESET, NULL);
            break;
            
        case GUI_INPUT_TOGGLE_GRID:
            gui_renderer_toggle_grid(client->renderer);
            break;
            
        case GUI_INPUT_TOGGLE_INFO:
            gui_renderer_toggle_info_panel(client->renderer);
            break;
            
        case GUI_INPUT_ZOOM_IN:
            gui_renderer_zoom_at(client->renderer, 
                                  client->renderer->window_width / 2,
                                  client->renderer->window_height / 2,
                                  1.2f);
            break;
            
        case GUI_INPUT_ZOOM_OUT:
            gui_renderer_zoom_at(client->renderer,
                                  client->renderer->window_width / 2,
                                  client->renderer->window_height / 2,
                                  0.8f);
            break;
            
        case GUI_INPUT_PAN_UP:
        {
            float z = client->renderer->zoom > 0.001f ? client->renderer->zoom : 1.0f;
            gui_renderer_pan(client->renderer, 0, -PAN_SPEED / z);
            break;
        }
            
        case GUI_INPUT_PAN_DOWN:
        {
            float z = client->renderer->zoom > 0.001f ? client->renderer->zoom : 1.0f;
            gui_renderer_pan(client->renderer, 0, PAN_SPEED / z);
            break;
        }
            
        case GUI_INPUT_PAN_LEFT:
        {
            float z = client->renderer->zoom > 0.001f ? client->renderer->zoom : 1.0f;
            gui_renderer_pan(client->renderer, -PAN_SPEED / z, 0);
            break;
        }
            
        case GUI_INPUT_PAN_RIGHT:
        {
            float z = client->renderer->zoom > 0.001f ? client->renderer->zoom : 1.0f;
            gui_renderer_pan(client->renderer, PAN_SPEED / z, 0);
            break;
        }
            
        case GUI_INPUT_CLICK:
            {
                float wx, wy;
                gui_renderer_screen_to_world(client->renderer, input->click_x, input->click_y, &wx, &wy);
                gui_client_select_colony_at(client, wx, wy);
            }
            break;
            
        case GUI_INPUT_SCROLL:
            {
                float factor = (input->scroll_delta > 0) ? 1.1f : 0.9f;
                gui_renderer_zoom_at(client->renderer, input->mouse_x, input->mouse_y, factor);
            }
            break;
            
        case GUI_INPUT_NONE:
        default:
            break;
    }
    
    // Handle mouse drag for panning (independent of action)
    if (input->mouse_right_down || input->mouse_middle_down) {
        float z = client->renderer->zoom > 0.001f ? client->renderer->zoom : 1.0f;
        gui_renderer_pan(client->renderer,
                         -input->mouse_dx / z,
                         -input->mouse_dy / z);
    }
}

static void gui_client_receive_updates(GuiClient* client) {
    if (!client->socket) return;
    
    // Drain all pending messages to keep up with high-speed simulation
    // This prevents buffer overflow when server sends faster than we render
    int messages_processed = 0;
    const int max_messages_per_frame = 50;  // Limit to prevent infinite loop
    
    while (net_has_data(client->socket) && messages_processed < max_messages_per_frame) {
        MessageHeader header;
        uint8_t* payload = NULL;
        
        int result = protocol_recv_message(client->socket->fd, &header, &payload);
        if (result == 0) {
            gui_client_handle_message(client, (MessageType)header.type, payload, header.payload_len);
            free(payload);
            messages_processed++;
        } else if (result < 0) {
            // Connection error
            fprintf(stderr, "Network receive error\n");
            client->connected = false;
            break;
        } else {
            // Partial read, try again next frame
            break;
        }
    }
}

static void gui_client_render(GuiClient* client) {
    gui_renderer_clear(client->renderer);
    
    // Draw world
    gui_renderer_draw_world(client->renderer, &client->local_world);
    
    // Draw colony info panel
    const ProtoColony* selected = gui_client_get_selected_colony(client);
    gui_renderer_draw_colony_info(client->renderer, selected);
    
    // Draw status bar
    int alive_count = 0;
    for (uint32_t i = 0; i < client->local_world.colony_count; i++) {
        if (client->local_world.colonies[i].alive) alive_count++;
    }
    
    gui_renderer_draw_status_bar(client->renderer,
                                  client->local_world.tick,
                                  alive_count,
                                  client->local_world.paused,
                                  client->local_world.speed_multiplier,
                                  client->fps);
    
    // Draw controls help hint
    gui_renderer_draw_controls_help(client->renderer);
    
    gui_renderer_present(client->renderer);
}

void gui_client_run(GuiClient* client) {
    if (!client) return;
    
    client->running = true;
    client->fps = 60.0f;  // Initial estimate
    
    Uint32 last_time = SDL_GetTicks();
    Uint32 fps_update_time = last_time;
    int frame_count = 0;
    
    while (client->running) {
        Uint32 current_time = SDL_GetTicks();
        float dt = (current_time - last_time) / 1000.0f;
        last_time = current_time;
        
        // Update FPS counter every 500ms
        frame_count++;
        if (current_time - fps_update_time >= 500) {
            client->fps = frame_count * 1000.0f / (current_time - fps_update_time);
            frame_count = 0;
            fps_update_time = current_time;
        }
        
        // Update animation time
        gui_renderer_update_time(client->renderer, dt);
        
        // Process input events
        GuiInputState input_state;
        if (!gui_input_process(&input_state)) {
            client->running = false;
            break;
        }
        gui_client_process_input(client, &input_state);
        
        // Check for held keys (continuous panning/zooming)
        const Uint8* keys = SDL_GetKeyboardState(NULL);
        float pan_speed = PAN_SPEED * 3.0f * dt;  // Scale by delta time
        
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
            gui_renderer_pan(client->renderer, 0, -pan_speed * 10.0f / client->renderer->zoom);
        }
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
            gui_renderer_pan(client->renderer, 0, pan_speed * 10.0f / client->renderer->zoom);
        }
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
            gui_renderer_pan(client->renderer, -pan_speed * 10.0f / client->renderer->zoom, 0);
        }
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
            gui_renderer_pan(client->renderer, pan_speed * 10.0f / client->renderer->zoom, 0);
        }
        
        // Continuous zoom with Z/X keys
        if (keys[SDL_SCANCODE_Z]) {
            gui_renderer_zoom_at(client->renderer,
                                  client->renderer->window_width / 2,
                                  client->renderer->window_height / 2,
                                  1.0f + dt * 2.0f);  // Zoom in
        }
        if (keys[SDL_SCANCODE_X]) {
            gui_renderer_zoom_at(client->renderer,
                                  client->renderer->window_width / 2,
                                  client->renderer->window_height / 2,
                                  1.0f - dt * 2.0f);  // Zoom out
        }
        
        // Receive network updates
        if (client->connected) {
            gui_client_receive_updates(client);
        }
        
        // Render
        gui_client_render(client);
        
        // Frame rate limiting (VSync should handle this, but just in case)
        Uint32 frame_time = SDL_GetTicks() - current_time;
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);
        }
    }
}
