/**
 * server.c - Server core implementation for bacterial colony simulator
 * Part of Phase 5: Server Implementation
 */

#include "server.h"
#include "simulation.h"
#include "parallel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <math.h>

// Protocol types - we need to include protocol.h but types.h already defined World/Colony
// So we include protocol.h here and use the types directly knowing they come from types.h (via world.h)
// The protocol functions work with the protocol-defined structures, so we'll build them inline
#include "../shared/protocol.h"

// Forward declarations for thread functions
static void* accept_thread_func(void* arg);
static void* simulation_thread_func(void* arg);

// Helper to get current time in milliseconds
static long get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

// Helper to sleep for milliseconds
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

Server* server_create(uint16_t port, int world_width, int world_height, int thread_count) {
    if (world_width <= 0 || world_height <= 0 || thread_count <= 0) {
        return NULL;
    }
    
    Server* server = (Server*)calloc(1, sizeof(Server));
    if (!server) return NULL;
    
    // Create network listener
    server->listener = net_server_create(port);
    if (!server->listener) {
        free(server);
        return NULL;
    }
    
    // Create world
    server->world = world_create(world_width, world_height);
    if (!server->world) {
        net_server_destroy(server->listener);
        free(server);
        return NULL;
    }
    
    // Create thread pool
    server->pool = threadpool_create(thread_count);
    if (!server->pool) {
        world_destroy(server->world);
        net_server_destroy(server->listener);
        free(server);
        return NULL;
    }
    
    // Create parallel context (4x4 regions by default)
    int regions = thread_count > 1 ? 4 : 2;
    server->parallel_ctx = parallel_create(server->pool, server->world, regions, regions);
    if (!server->parallel_ctx) {
        threadpool_destroy(server->pool);
        world_destroy(server->world);
        net_server_destroy(server->listener);
        free(server);
        return NULL;
    }
    parallel_init_regions(server->parallel_ctx, world_width, world_height);
    
    // Create atomic world for lock-free parallel simulation
    server->atomic_world = atomic_world_create(server->world, server->pool, thread_count);
    if (!server->atomic_world) {
        parallel_destroy(server->parallel_ctx);
        threadpool_destroy(server->pool);
        world_destroy(server->world);
        net_server_destroy(server->listener);
        free(server);
        return NULL;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&server->clients_mutex, NULL) != 0) {
        atomic_world_destroy(server->atomic_world);
        parallel_destroy(server->parallel_ctx);
        threadpool_destroy(server->pool);
        world_destroy(server->world);
        net_server_destroy(server->listener);
        free(server);
        return NULL;
    }
    
    // Initialize state
    server->clients = NULL;
    server->client_count = 0;
    server->running = false;
    server->paused = false;
    server->tick_rate_ms = DEFAULT_TICK_RATE_MS;
    server->speed_multiplier = 1.0f;
    server->next_client_id = 1;
    
    return server;
}

void server_destroy(Server* server) {
    if (!server) return;
    
    // Stop if running
    if (server->running) {
        server_stop(server);
    }
    
    // Clean up all clients
    pthread_mutex_lock(&server->clients_mutex);
    ClientSession* client = server->clients;
    while (client) {
        ClientSession* next = client->next;
        if (client->socket) {
            net_socket_close(client->socket);
        }
        free(client);
        client = next;
    }
    server->clients = NULL;
    server->client_count = 0;
    pthread_mutex_unlock(&server->clients_mutex);
    
    // Destroy resources
    pthread_mutex_destroy(&server->clients_mutex);
    
    if (server->atomic_world) {
        atomic_world_destroy(server->atomic_world);
    }
    if (server->parallel_ctx) {
        parallel_destroy(server->parallel_ctx);
    }
    if (server->pool) {
        threadpool_destroy(server->pool);
    }
    if (server->world) {
        world_destroy(server->world);
    }
    if (server->listener) {
        net_server_destroy(server->listener);
    }
    
    free(server);
}

static void* accept_thread_func(void* arg) {
    Server* server = (Server*)arg;
    
    while (server->running) {
        // Accept new connection (blocking)
        NetSocket* socket = net_server_accept(server->listener);
        if (!socket) {
            if (!server->running) break;
            continue;
        }
        
        // Set socket options
        net_set_nonblocking(socket, true);
        net_set_nodelay(socket, true);
        
        // Add client
        ClientSession* client = server_add_client(server, socket);
        if (client) {
            printf("Client %u connected from %s:%u\n", 
                   client->id, socket->address, socket->port);
        } else {
            net_socket_close(socket);
        }
    }
    
    return NULL;
}

static void* simulation_thread_func(void* arg) {
    Server* server = (Server*)arg;
    
    while (server->running) {
        long start_time = get_time_ms();
        
        if (!server->paused) {
            // Run simulation tick using atomic lock-free parallel processing
            atomic_tick(server->atomic_world);
            
            // Broadcast world state to all clients
            server_broadcast_world_state(server);
        }
        
        // Process client messages
        server_process_clients(server);
        
        // Calculate sleep time to maintain tick rate
        long elapsed = get_time_ms() - start_time;
        // Ensure target_ms is at least 1ms to prevent busy-waiting and timing issues
        float speed = server->speed_multiplier;
        if (speed < 0.1f) speed = 0.1f;  // Clamp to prevent division issues
        int target_ms = (int)(server->tick_rate_ms / speed);
        if (target_ms < 1) target_ms = 1;  // Minimum 1ms tick to prevent CPU spinning
        if (elapsed < target_ms) {
            sleep_ms(target_ms - (int)elapsed);
        }
    }
    
    return NULL;
}

void server_run(Server* server) {
    if (!server || server->running) return;
    
    server->running = true;
    
    // Start accept thread
    if (pthread_create(&server->accept_thread, NULL, accept_thread_func, server) != 0) {
        server->running = false;
        return;
    }
    
    // Run simulation in current thread (blocking)
    simulation_thread_func(server);
    
    // Wait for accept thread
    pthread_join(server->accept_thread, NULL);
}

void server_stop(Server* server) {
    if (!server || !server->running) return;
    
    server->running = false;
    
    // Close listener to unblock accept
    if (server->listener) {
        net_server_destroy(server->listener);
        server->listener = NULL;
    }
}

// Convert internal World/Colony to protocol ProtoWorld for serialization
static void build_protocol_world(Server* server, ProtoWorld* proto_world) {
    memset(proto_world, 0, sizeof(ProtoWorld));
    
    proto_world->width = (uint32_t)server->world->width;
    proto_world->height = (uint32_t)server->world->height;
    proto_world->tick = (uint32_t)server->world->tick;
    proto_world->paused = server->paused;
    proto_world->speed_multiplier = server->speed_multiplier;
    
    // Count active colonies
    uint32_t count = 0;
    for (size_t i = 0; i < server->world->colony_count && count < MAX_COLONIES; i++) {
        if (server->world->colonies[i].active) {
            ProtoColony* proto_colony = &proto_world->colonies[count];
            
            // Map fields from internal Colony (types.h) to protocol ProtoColony
            proto_colony->id = server->world->colonies[i].id;
            strncpy(proto_colony->name, server->world->colonies[i].name, MAX_COLONY_NAME - 1);
            proto_colony->name[MAX_COLONY_NAME - 1] = '\0';
            
            // Calculate centroid of colony cells for x,y position
            float sum_x = 0, sum_y = 0;
            int cell_count = 0;
            for (int cy = 0; cy < server->world->height; cy++) {
                for (int cx = 0; cx < server->world->width; cx++) {
                    Cell* cell = world_get_cell(server->world, cx, cy);
                    if (cell && cell->colony_id == server->world->colonies[i].id) {
                        sum_x += (float)cx;
                        sum_y += (float)cy;
                        cell_count++;
                    }
                }
            }
            if (cell_count > 0) {
                proto_colony->x = sum_x / (float)cell_count;
                proto_colony->y = sum_y / (float)cell_count;
            }
            
            proto_colony->population = (uint32_t)server->world->colonies[i].cell_count;
            proto_colony->max_population = (uint32_t)server->world->colonies[i].max_cell_count;
            proto_colony->radius = (float)server->world->colonies[i].cell_count / 3.14159f;
            if (proto_colony->radius > 0) proto_colony->radius = sqrtf(proto_colony->radius);
            proto_colony->growth_rate = server->world->colonies[i].genome.spread_rate;
            proto_colony->color_r = server->world->colonies[i].color.r;
            proto_colony->color_g = server->world->colonies[i].color.g;
            proto_colony->color_b = server->world->colonies[i].color.b;
            proto_colony->alive = server->world->colonies[i].active;
            
            // Copy shape data for procedural organic borders
            proto_colony->shape_seed = server->world->colonies[i].shape_seed;
            proto_colony->wobble_phase = server->world->colonies[i].wobble_phase;
            proto_colony->shape_evolution = server->world->colonies[i].shape_evolution;
            
            count++;
        }
    }
    proto_world->colony_count = count;
}

void server_broadcast_world_state(Server* server) {
    if (!server) return;
    
    // Build protocol world state
    ProtoWorld proto_world;
    build_protocol_world(server, &proto_world);
    
    // Serialize
    uint8_t* buffer = NULL;
    size_t len = 0;
    if (protocol_serialize_world_state(&proto_world, &buffer, &len) < 0) {
        return;
    }
    
    // Broadcast to all clients
    pthread_mutex_lock(&server->clients_mutex);
    ClientSession* client = server->clients;
    ClientSession* prev = NULL;
    
    while (client) {
        ClientSession* next = client->next;
        
        if (client->active && client->socket && client->socket->connected) {
            int result = protocol_send_message(client->socket->fd, MSG_WORLD_STATE, buffer, len);
            if (result < 0) {
                // Client disconnected
                printf("Client %u disconnected\n", client->id);
                client->active = false;
                
                // Remove from list
                if (prev) {
                    prev->next = next;
                } else {
                    server->clients = next;
                }
                
                net_socket_close(client->socket);
                free(client);
                server->client_count--;
                
                client = next;
                continue;
            }
        }
        
        prev = client;
        client = next;
    }
    pthread_mutex_unlock(&server->clients_mutex);
    
    free(buffer);
}

void server_send_colony_info(Server* server, ClientSession* client, uint32_t colony_id) {
    if (!server || !client || !client->socket || colony_id == 0) return;
    
    // Find colony index in internal world
    size_t colony_idx = 0;
    bool found = false;
    for (size_t i = 0; i < server->world->colony_count; i++) {
        if (server->world->colonies[i].id == colony_id && server->world->colonies[i].active) {
            colony_idx = i;
            found = true;
            break;
        }
    }
    
    if (!found) return;
    
    // Build protocol colony
    ProtoColony proto_colony;
    memset(&proto_colony, 0, sizeof(ProtoColony));
    proto_colony.id = colony_id;
    strncpy(proto_colony.name, server->world->colonies[colony_idx].name, MAX_COLONY_NAME - 1);
    proto_colony.name[MAX_COLONY_NAME - 1] = '\0';
    proto_colony.population = (uint32_t)server->world->colonies[colony_idx].cell_count;
    proto_colony.alive = server->world->colonies[colony_idx].active;
    
    uint8_t buffer[COLONY_SERIALIZED_SIZE];
    int len = protocol_serialize_colony(&proto_colony, buffer);
    if (len > 0) {
        protocol_send_message(client->socket->fd, MSG_COLONY_INFO, buffer, (size_t)len);
    }
}

void server_handle_command(Server* server, ClientSession* client, CommandType cmd, void* data) {
    if (!server || !client) return;
    
    switch (cmd) {
        case CMD_PAUSE:
            server->paused = true;
            printf("Server paused by client %u\n", client->id);
            break;
            
        case CMD_RESUME:
            server->paused = false;
            printf("Server resumed by client %u\n", client->id);
            break;
            
        case CMD_SPEED_UP:
            if (server->speed_multiplier < 10.0f) {
                server->speed_multiplier *= 2.0f;
                // Clamp to max to handle floating point accumulation
                if (server->speed_multiplier > 10.0f) server->speed_multiplier = 10.0f;
                printf("Speed increased to %.1fx by client %u\n", server->speed_multiplier, client->id);
            }
            break;
            
        case CMD_SLOW_DOWN:
            if (server->speed_multiplier > 0.1f) {
                server->speed_multiplier /= 2.0f;
                // Clamp to min to handle floating point accumulation
                if (server->speed_multiplier < 0.1f) server->speed_multiplier = 0.1f;
                printf("Speed decreased to %.1fx by client %u\n", server->speed_multiplier, client->id);
            }
            break;
            
        case CMD_RESET:
            // Reset world
            world_destroy(server->world);
            server->world = world_create(server->parallel_ctx->regions[0].end_x * server->parallel_ctx->regions_x,
                                          server->parallel_ctx->regions[0].end_y * server->parallel_ctx->regions_y);
            if (server->world) {
                world_init_random_colonies(server->world, 5);
            }
            printf("World reset by client %u\n", client->id);
            break;
            
        case CMD_SELECT_COLONY:
            if (data) {
                CommandSelectColony* sel = (CommandSelectColony*)data;
                client->selected_colony = sel->colony_id;
                server_send_colony_info(server, client, sel->colony_id);
            }
            break;
            
        case CMD_SPAWN_COLONY:
            if (data) {
                CommandSpawnColony* spawn = (CommandSpawnColony*)data;
                // Create a simple colony at the specified position
                // This would need proper implementation using world_add_colony
                printf("Spawn colony request at (%.1f, %.1f) by client %u\n", 
                       spawn->x, spawn->y, client->id);
            }
            break;
    }
}

ClientSession* server_add_client(Server* server, NetSocket* socket) {
    if (!server || !socket) return NULL;
    
    ClientSession* session = (ClientSession*)calloc(1, sizeof(ClientSession));
    if (!session) return NULL;
    
    session->socket = socket;
    session->active = true;
    session->selected_colony = 0;
    
    pthread_mutex_lock(&server->clients_mutex);
    session->id = server->next_client_id++;
    session->next = server->clients;
    server->clients = session;
    server->client_count++;
    pthread_mutex_unlock(&server->clients_mutex);
    
    return session;
}

void server_remove_client(Server* server, ClientSession* client) {
    if (!server || !client) return;
    
    pthread_mutex_lock(&server->clients_mutex);
    
    ClientSession* prev = NULL;
    ClientSession* curr = server->clients;
    
    while (curr) {
        if (curr == client) {
            if (prev) {
                prev->next = curr->next;
            } else {
                server->clients = curr->next;
            }
            
            if (curr->socket) {
                net_socket_close(curr->socket);
            }
            free(curr);
            server->client_count--;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&server->clients_mutex);
}

void server_process_clients(Server* server) {
    if (!server) return;
    
    pthread_mutex_lock(&server->clients_mutex);
    ClientSession* client = server->clients;
    
    while (client) {
        ClientSession* next = client->next;
        
        if (!client->active || !client->socket || !client->socket->connected) {
            client = next;
            continue;
        }
        
        // Check for incoming data
        if (net_has_data(client->socket)) {
            MessageHeader header;
            uint8_t* payload = NULL;
            
            // Set socket to blocking temporarily for complete message read
            net_set_nonblocking(client->socket, false);
            int result = protocol_recv_message(client->socket->fd, &header, &payload);
            net_set_nonblocking(client->socket, true);
            
            if (result < 0) {
                // Client disconnected or error
                printf("Client %u disconnected\n", client->id);
                client->active = false;
            } else {
                // Process message
                switch (header.type) {
                    case MSG_COMMAND: {
                        CommandType cmd;
                        uint8_t cmd_data[256];
                        if (protocol_deserialize_command(payload, &cmd, cmd_data) > 0) {
                            server_handle_command(server, client, cmd, cmd_data);
                        }
                        break;
                    }
                    case MSG_DISCONNECT:
                        printf("Client %u requested disconnect\n", client->id);
                        client->active = false;
                        break;
                    default:
                        break;
                }
                
                if (payload) free(payload);
            }
        }
        
        client = next;
    }
    
    pthread_mutex_unlock(&server->clients_mutex);
}

uint16_t server_get_port(Server* server) {
    if (!server || !server->listener) return 0;
    return server->listener->port;
}
