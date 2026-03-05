/**
 * server.c - Server core implementation for bacterial colony simulator
 * Part of Phase 5: Server Implementation
 */

#include "server.h"
#include "simulation.h"
#include "genetics.h"
#include "parallel.h"
#include "../shared/names.h"
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
static bool server_rebuild_world_state(Server* server, int width, int height, int initial_colonies);
static bool server_spawn_colony(Server* server, const CommandSpawnColony* spawn);

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
    
    // Initialize mutexes
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
    atomic_init(&server->running, false);
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
    client_session* client = server->clients;
    while (client) {
        client_session* next = client->next;
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
    
    bool running = atomic_load(&server->running);
    
    while (running) {
        // Accept new connection (blocking)
        net_socket* socket = net_server_accept(server->listener);
        if (!socket) {
            running = atomic_load(&server->running);
            if (!running) break;
            continue;
        }
        
        // Set socket options
        net_set_nonblocking(socket, true);
        net_set_nodelay(socket, true);
        
        // Add client
        client_session* client = server_add_client(server, socket);
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
    
    int tick_counter = 0;
    
    bool running = atomic_load(&server->running);
    
    while (running) {
        long start_time = get_time_ms();
        
        if (!server->paused) {
            // Run simulation tick using atomic lock-free parallel processing
            atomic_tick(server->atomic_world);
            tick_counter++;
            
            // At high speeds, skip some broadcasts to avoid flooding the network.
            // Aim for ~15-30 broadcasts/sec max regardless of simulation speed.
            int broadcast_every = 1;
            float speed = server->speed_multiplier;
            if (speed > 4.0f) broadcast_every = (int)(speed / 4.0f);
            if (broadcast_every > 16) broadcast_every = 16;
            
            if (tick_counter % broadcast_every == 0) {
                server_broadcast_world_state(server);
            }
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
        
        running = atomic_load(&server->running);
    }
    
    return NULL;
}

void server_run(Server* server) {
    if (!server) return;
    
    if (atomic_load(&server->running)) {
        return;
    }
    atomic_store(&server->running, true);
    
    // Start accept thread
    if (pthread_create(&server->accept_thread, NULL, accept_thread_func, server) != 0) {
        atomic_store(&server->running, false);
        return;
    }
    
    // Run simulation in current thread (blocking)
    simulation_thread_func(server);
    
    // Wait for accept thread
    pthread_join(server->accept_thread, NULL);
}

void server_stop(Server* server) {
    if (!server) return;
    
    if (!atomic_load(&server->running)) {
        return;
    }
    atomic_store(&server->running, false);
    
    // Close listener to unblock accept
    if (server->listener) {
        net_server_stop(server->listener);
    }
}

static bool server_rebuild_world_state(Server* server, int width, int height, int initial_colonies) {
    if (!server || width <= 0 || height <= 0) {
        return false;
    }

    World* new_world = world_create(width, height);
    if (!new_world) {
        return false;
    }

    if (server->world) {
        RDSolverControls controls = world_get_rd_controls(server->world);
        if (!world_set_rd_controls(new_world, &controls, NULL, 0)) {
            world_destroy(new_world);
            return false;
        }
    }

    if (initial_colonies > 0) {
        world_init_random_colonies(new_world, initial_colonies);
    }

    int regions = server->pool && server->pool->thread_count > 1 ? 4 : 2;
    ParallelContext* new_parallel = parallel_create(server->pool, new_world, regions, regions);
    if (!new_parallel) {
        world_destroy(new_world);
        return false;
    }
    parallel_init_regions(new_parallel, width, height);

    int thread_count = server->pool ? server->pool->thread_count : 1;
    AtomicWorld* new_atomic = atomic_world_create(new_world, server->pool, thread_count);
    if (!new_atomic) {
        parallel_destroy(new_parallel);
        world_destroy(new_world);
        return false;
    }

    AtomicWorld* old_atomic = server->atomic_world;
    ParallelContext* old_parallel = server->parallel_ctx;
    World* old_world = server->world;

    server->world = new_world;
    server->parallel_ctx = new_parallel;
    server->atomic_world = new_atomic;

    if (old_atomic) {
        atomic_world_destroy(old_atomic);
    }
    if (old_parallel) {
        parallel_destroy(old_parallel);
    }
    if (old_world) {
        world_destroy(old_world);
    }

    return true;
}

// Convert internal World/Colony to protocol proto_world for serialization
static void build_protocol_world(Server* server, proto_world* proto_world) {
    proto_world_init(proto_world);
    World* world = server->world;
    
    proto_world->width = (uint32_t)world->width;
    proto_world->height = (uint32_t)world->height;
    proto_world->tick = (uint32_t)world->tick;
    proto_world->paused = server->paused;
    proto_world->speed_multiplier = server->speed_multiplier;
    
    // Build grid data from world cells
    uint32_t grid_size = proto_world->width * proto_world->height;
    if (grid_size > 0 && grid_size <= MAX_GRID_SIZE) {
        proto_world_alloc_grid(proto_world, proto_world->width, proto_world->height);
        if (proto_world->grid) {
            for (uint32_t i = 0; i < grid_size; i++) {
                proto_world->grid[i] = (uint16_t)world->cells[i].colony_id;
            }
        }
    }

    int32_t* world_idx_to_proto = NULL;
    if (world->colony_count > 0) {
        world_idx_to_proto = (int32_t*)malloc(world->colony_count * sizeof(int32_t));
        if (world_idx_to_proto) {
            for (size_t i = 0; i < world->colony_count; i++) {
                world_idx_to_proto[i] = -1;
            }
        }
    }
    
    // Build protocol colony list and world-index mapping.
    uint32_t count = 0;
    for (size_t i = 0; i < world->colony_count && count < MAX_COLONIES; i++) {
        if (world->colonies[i].active) {
            proto_colony* proto_colony = &proto_world->colonies[count];
            
            // Map fields from internal Colony (types.h) to protocol proto_colony
            proto_colony->id = world->colonies[i].id;
            strncpy(proto_colony->name, world->colonies[i].name, MAX_COLONY_NAME - 1);
            proto_colony->name[MAX_COLONY_NAME - 1] = '\0';

            if (world_idx_to_proto) {
                world_idx_to_proto[i] = (int32_t)count;
            }

            proto_colony->x = world->colonies[i].centroid_x;
            proto_colony->y = world->colonies[i].centroid_y;
            
            proto_colony->population = (uint32_t)world->colonies[i].cell_count;
            proto_colony->max_population = (uint32_t)world->colonies[i].max_cell_count;
            proto_colony->radius = (float)world->colonies[i].cell_count / 3.14159f;
            if (proto_colony->radius > 0) proto_colony->radius = sqrtf(proto_colony->radius);
            proto_colony->growth_rate = world->colonies[i].genome.spread_rate;
            proto_colony->color_r = world->colonies[i].color.r;
            proto_colony->color_g = world->colonies[i].color.g;
            proto_colony->color_b = world->colonies[i].color.b;
            proto_colony->alive = world->colonies[i].active;
            
            // Copy shape data (kept for compatibility, may be removed later)
            proto_colony->shape_seed = world->colonies[i].shape_seed;
            proto_colony->wobble_phase = world->colonies[i].wobble_phase;
            proto_colony->shape_evolution = world->colonies[i].shape_evolution;
            
            // Copy key trait data for info panel display
            proto_colony->aggression = world->colonies[i].genome.aggression;
            proto_colony->defense = world->colonies[i].genome.defense_priority;
            proto_colony->metabolism = world->colonies[i].genome.metabolism;
            proto_colony->toxin_production = world->colonies[i].genome.toxin_production;
            proto_colony->spread_rate = world->colonies[i].genome.spread_rate;
            
            count++;
        }
    }

    if (count > 0 && world_idx_to_proto) {
        double* sum_x = (double*)calloc(count, sizeof(double));
        double* sum_y = (double*)calloc(count, sizeof(double));
        uint32_t* pop = (uint32_t*)calloc(count, sizeof(uint32_t));

        if (sum_x && sum_y && pop) {
            for (uint32_t i = 0; i < grid_size; i++) {
                uint32_t colony_id = world->cells[i].colony_id;
                if (colony_id == 0) continue;

                Colony* colony = world_get_colony(world, colony_id);
                if (!colony) continue;

                size_t world_idx = (size_t)(colony - world->colonies);
                if (world_idx >= world->colony_count) continue;

                int32_t proto_idx = world_idx_to_proto[world_idx];
                if (proto_idx < 0) continue;

                uint32_t pidx = (uint32_t)proto_idx;
                pop[pidx]++;
                sum_x[pidx] += (double)(i % proto_world->width);
                sum_y[pidx] += (double)(i / proto_world->width);
            }

            for (uint32_t i = 0; i < count; i++) {
                if (pop[i] > 0) {
                    proto_world->colonies[i].x = (float)(sum_x[i] / (double)pop[i]);
                    proto_world->colonies[i].y = (float)(sum_y[i] / (double)pop[i]);
                }
            }
        }

        free(pop);
        free(sum_y);
        free(sum_x);
    }

    free(world_idx_to_proto);

    proto_world->colony_count = count;
}

void server_broadcast_world_state(Server* server) {
    if (!server) return;
    
    // Build protocol world state
    proto_world proto_world;
    build_protocol_world(server, &proto_world);
    
    // Serialize
    uint8_t* buffer = NULL;
    size_t len = 0;
    if (protocol_serialize_world_state(&proto_world, &buffer, &len) < 0) {
        proto_world_free(&proto_world);
        return;
    }
    
    // Snapshot clients under lock so socket I/O does not block client-list mutation.
    client_session** snapshot = NULL;
    int snapshot_count = 0;
    pthread_mutex_lock(&server->clients_mutex);
    if (server->client_count > 0) {
        snapshot = (client_session**)calloc((size_t)server->client_count, sizeof(client_session*));
        if (snapshot) {
            client_session* client = server->clients;
            while (client && snapshot_count < server->client_count) {
                snapshot[snapshot_count++] = client;
                client = client->next;
            }
        }
    }
    pthread_mutex_unlock(&server->clients_mutex);

    for (int i = 0; i < snapshot_count; i++) {
        client_session* client = snapshot[i];
        if (!client || !client->active || !client->socket || !client->socket->connected) continue;
        if (protocol_send_message(client->socket->fd, MSG_WORLD_STATE, buffer, len) < 0) {
            printf("Client %u disconnected\n", client->id);
            client->active = false;
        }
    }

    pthread_mutex_lock(&server->clients_mutex);
    client_session* client = server->clients;
    client_session* prev = NULL;
    while (client) {
        client_session* next = client->next;
        if (!client->active || !client->socket || !client->socket->connected) {
            if (prev) {
                prev->next = next;
            } else {
                server->clients = next;
            }
            if (client->socket) {
                net_socket_close(client->socket);
            }
            free(client);
            server->client_count--;
            client = next;
            continue;
        }
        prev = client;
        client = next;
    }
    pthread_mutex_unlock(&server->clients_mutex);
    
    free(snapshot);
    free(buffer);
    proto_world_free(&proto_world);
}

void server_send_colony_info(Server* server, client_session* client, uint32_t colony_id) {
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
    proto_colony proto_colony;
    memset(&proto_colony, 0, sizeof(proto_colony));
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

void server_handle_command(Server* server, client_session* client, CommandType cmd, void* data) {
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
            if (server->speed_multiplier < 100.0f) {
                server->speed_multiplier *= 2.0f;
                // Clamp to max to handle floating point accumulation
                if (server->speed_multiplier > 100.0f) server->speed_multiplier = 100.0f;
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
            // Reset world while preserving dimensions
            if (server->world) {
                int width = server->world->width;
                int height = server->world->height;
                if (!server_rebuild_world_state(server, width, height, 5)) {
                    fprintf(stderr, "Failed to reset world for client %u\n", client->id);
                    break;
                }
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
                if (server_spawn_colony(server, spawn)) {
                    printf("Spawned colony at (%.1f, %.1f) for client %u\n",
                           spawn->x, spawn->y, client->id);
                } else {
                    printf("Spawn colony request rejected at (%.1f, %.1f) by client %u\n",
                           spawn->x, spawn->y, client->id);
                }
            }
            break;
    }
}

static bool server_spawn_colony(Server* server, const CommandSpawnColony* spawn) {
    if (!server || !server->world || !spawn) return false;

    int x = (int)spawn->x;
    int y = (int)spawn->y;
    Cell* cell = world_get_cell(server->world, x, y);
    if (!cell || cell->colony_id != 0) return false;

    Colony colony;
    memset(&colony, 0, sizeof(colony));
    colony.genome = genome_create_random();
    colony.color = colony.genome.body_color;
    colony.cell_count = 1;
    colony.max_cell_count = 1;
    colony.active = true;
    colony.shape_seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
    colony.wobble_phase = (float)(rand() % 628) / 100.0f;
    colony.shape_evolution = (float)(rand() % 1000) / 1000.0f;
    colony.state = COLONY_STATE_NORMAL;

    if (spawn->name[0] != '\0') {
        strncpy(colony.name, spawn->name, sizeof(colony.name) - 1);
        colony.name[sizeof(colony.name) - 1] = '\0';
    } else {
        generate_scientific_name(colony.name, sizeof(colony.name));
    }

    uint32_t id = world_add_colony(server->world, colony);
    if (id == 0) return false;

    cell->colony_id = id;
    cell->age = 0;
    cell->is_border = true;

    Colony* added = world_get_colony(server->world, id);
    if (added) {
        added->cell_count = 1;
        added->max_cell_count = 1;
    }

    if (server->atomic_world) {
        atomic_world_sync_from_world(server->atomic_world);
    }
    return true;
}

client_session* server_add_client(Server* server, net_socket* socket) {
    if (!server || !socket) return NULL;
    
    client_session* session = (client_session*)calloc(1, sizeof(client_session));
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

void server_remove_client(Server* server, client_session* client) {
    if (!server || !client) return;
    
    pthread_mutex_lock(&server->clients_mutex);
    
    client_session* prev = NULL;
    client_session* curr = server->clients;
    
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
    client_session* client = server->clients;
    
    while (client) {
        client_session* next = client->next;
        
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
                        if (protocol_deserialize_command(payload, header.payload_len, &cmd, cmd_data) > 0) {
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
