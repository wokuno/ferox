/**
 * server.c - Server core implementation for bacterial colony simulator
 * Part of Phase 5: Server Implementation
 */

#include "server.h"
#include "simulation.h"
#include "parallel.h"
#include "genetics.h"
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
#include "../shared/names.h"

// Forward declarations for thread functions
static void* accept_thread_func(void* arg);
static void* simulation_thread_func(void* arg);

static void copy_colony_name(char dst[MAX_COLONY_NAME], const char* src) {
    if (!dst) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t len = strlen(src);
    if (len >= MAX_COLONY_NAME) {
        len = MAX_COLONY_NAME - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void copy_spawn_colony_name(char* dst, size_t dst_size,
                                   const char src[MAX_COLONY_NAME]) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t len = 0;
    while (len < MAX_COLONY_NAME && src[len] != '\0') {
        len++;
    }
    if (len >= dst_size) {
        len = dst_size - 1;
    }

    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void server_fill_command_status(ProtoCommandStatus* status,
                                       CommandType cmd,
                                       ProtoCommandStatusCode code,
                                       uint32_t entity_id,
                                       const char* message) {
    if (!status) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->command = (uint32_t)cmd;
    status->status_code = (uint32_t)code;
    status->entity_id = entity_id;
    if (message) {
        copy_spawn_colony_name(status->message, sizeof(status->message), message);
    }
}

static void server_send_command_status(Server* server,
                                       ClientSession* client,
                                       MessageType type,
                                       const ProtoCommandStatus* status) {
    (void)server;
    if (!client || !client->socket || client->socket->fd < 0 || !status) {
        return;
    }

    uint8_t buffer[COMMAND_STATUS_SERIALIZED_SIZE];
    int len = protocol_serialize_command_status(status, buffer);
    if (len > 0) {
        protocol_send_message(client->socket->fd, type, buffer, (size_t)len);
    }
}

static MessageType server_spawn_colony(Server* server,
                                       const CommandSpawnColony* spawn,
                                       ProtoCommandStatus* status) {
    server_fill_command_status(status, CMD_SPAWN_COLONY,
                               PROTO_COMMAND_STATUS_INTERNAL_ERROR,
                               0,
                               "Spawn failed");
    if (!server || !server->world || !spawn || !status) {
        return MSG_ERROR;
    }

    int x = (int)lroundf(spawn->x);
    int y = (int)lroundf(spawn->y);
    Cell* cell = world_get_cell(server->world, x, y);
    if (!cell) {
        server_fill_command_status(status, CMD_SPAWN_COLONY,
                                   PROTO_COMMAND_STATUS_OUT_OF_BOUNDS,
                                   0,
                                   "Spawn rejected: out of bounds");
        return MSG_ERROR;
    }
    if (cell->colony_id != 0) {
        server_fill_command_status(status, CMD_SPAWN_COLONY,
                                   PROTO_COMMAND_STATUS_CONFLICT,
                                   cell->colony_id,
                                   "Spawn rejected: occupied target");
        return MSG_ERROR;
    }

    Colony colony;
    memset(&colony, 0, sizeof(colony));
    if (spawn->name[0] != '\0') {
        copy_spawn_colony_name(colony.name, sizeof(colony.name), spawn->name);
    } else {
        generate_scientific_name(colony.name, sizeof(colony.name));
    }
    colony.genome = genome_create_random();
    colony.color = colony.genome.body_color;
    colony.cell_count = 1;
    colony.max_cell_count = 1;
    colony.active = true;
    colony.age = 0;
    colony.parent_id = 0;
    colony.shape_seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
    colony.wobble_phase = (float)(rand() % 628) / 100.0f;

    uint32_t colony_id = world_add_colony(server->world, colony);
    if (colony_id == 0) {
        server_fill_command_status(status, CMD_SPAWN_COLONY,
                                   PROTO_COMMAND_STATUS_INTERNAL_ERROR,
                                   0,
                                   "Spawn failed: colony allocation");
        return MSG_ERROR;
    }

    uint32_t cell_idx = (uint32_t)(y * server->world->width + x);
    cell->colony_id = colony_id;
    cell->age = 0;
    cell->is_border = false;
    world_colony_add_cell(server->world, colony_id, cell_idx);

    if (server->atomic_world) {
        atomic_world_sync_from_world(server->atomic_world);
    }

    server_fill_command_status(status, CMD_SPAWN_COLONY,
                               PROTO_COMMAND_STATUS_ACCEPTED,
                               colony_id,
                               "Spawn accepted");
    return MSG_ACK;
}

static float clamp_unit(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float summarize_trait_expansion(const Genome* genome) {
    return clamp_unit((genome->spread_rate + genome->metabolism + genome->motility) / 3.0f);
}

static float summarize_trait_aggression(const Genome* genome) {
    return clamp_unit((genome->aggression + genome->toxin_production + (1.0f - genome->merge_affinity)) / 3.0f);
}

static float summarize_trait_resilience(const Genome* genome) {
    return clamp_unit((genome->resilience + genome->toxin_resistance + genome->dormancy_resistance + genome->biofilm_investment) / 4.0f);
}

static float summarize_trait_cooperation(const Genome* genome) {
    float transfer = genome->gene_transfer_rate * 10.0f;
    return clamp_unit((genome->merge_affinity + genome->signal_emission + genome->signal_sensitivity + clamp_unit(transfer)) / 4.0f);
}

static float summarize_trait_efficiency(const Genome* genome) {
    return clamp_unit((genome->efficiency + genome->density_tolerance + (1.0f - genome->resource_consumption)) / 3.0f);
}

static float summarize_trait_learning(const Genome* genome) {
    return clamp_unit((genome->learning_rate + genome->memory_factor) * 0.5f);
}

static void fill_proto_colony_graph_links(const Colony* colony, ProtoColonyDetail* detail) {
    if (!colony || !detail) {
        return;
    }

    float best_sensor_drive = -1.0f;
    float second_sensor_drive = -1.0f;
    for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
        for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
            float centered_sensor = colony->behavior_sensors[sensor] * 2.0f - 1.0f;
            float contribution = centered_sensor * colony->genome.behavior_drive_weights[drive][sensor];
            float magnitude = fabsf(contribution);
            if (magnitude > best_sensor_drive) {
                second_sensor_drive = best_sensor_drive;
                detail->secondary_sensor_link_sensor = detail->sensor_link_sensor;
                detail->secondary_sensor_link_drive = detail->sensor_link_drive;
                detail->secondary_sensor_link_value = detail->sensor_link_value;
                best_sensor_drive = magnitude;
                detail->sensor_link_sensor = (uint8_t)sensor;
                detail->sensor_link_drive = (uint8_t)drive;
                detail->sensor_link_value = contribution;
            } else if (magnitude > second_sensor_drive) {
                second_sensor_drive = magnitude;
                detail->secondary_sensor_link_sensor = (uint8_t)sensor;
                detail->secondary_sensor_link_drive = (uint8_t)drive;
                detail->secondary_sensor_link_value = contribution;
            }
        }
    }

    float best_drive_action = -1.0f;
    float second_drive_action = -1.0f;
    for (int action = 0; action < COLONY_ACTION_COUNT; action++) {
        for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
            float centered_drive = colony->behavior_drives[drive] * 2.0f - 1.0f;
            float contribution = centered_drive * colony->genome.behavior_action_weights[action][drive];
            float magnitude = fabsf(contribution);
            if (magnitude > best_drive_action) {
                second_drive_action = best_drive_action;
                detail->secondary_action_link_drive = detail->action_link_drive;
                detail->secondary_action_link_action = detail->action_link_action;
                detail->secondary_action_link_value = detail->action_link_value;
                best_drive_action = magnitude;
                detail->action_link_drive = (uint8_t)drive;
                detail->action_link_action = (uint8_t)action;
                detail->action_link_value = contribution;
            } else if (magnitude > second_drive_action) {
                second_drive_action = magnitude;
                detail->secondary_action_link_drive = (uint8_t)drive;
                detail->secondary_action_link_action = (uint8_t)action;
                detail->secondary_action_link_value = contribution;
            }
        }
    }
}

static void fill_proto_colony_detail_base(const World* world,
                                          const Colony* colony,
                                          ProtoColony* proto_colony) {
    memset(proto_colony, 0, sizeof(*proto_colony));
    proto_colony->id = colony->id;
    copy_colony_name(proto_colony->name, colony->name);
    proto_colony->population = (uint32_t)colony->cell_count;
    proto_colony->max_population = (uint32_t)colony->max_cell_count;
    proto_colony->growth_rate = colony->genome.spread_rate;
    proto_colony->color_r = colony->color.r;
    proto_colony->color_g = colony->color.g;
    proto_colony->color_b = colony->color.b;
    proto_colony->alive = colony->active;
    proto_colony->shape_seed = colony->shape_seed;
    proto_colony->wobble_phase = colony->wobble_phase;
    proto_colony->shape_evolution = colony->shape_evolution;
    proto_colony->radius = colony->cell_count > 0 ? sqrtf((float)colony->cell_count / 3.14159f) : 0.0f;

    if (!world || colony->cell_count == 0) {
        return;
    }

    float sum_x = 0.0f;
    float sum_y = 0.0f;
    uint32_t cells = 0;
    for (int y = 0; y < world->height; y++) {
        int row_base = y * world->width;
        for (int x = 0; x < world->width; x++) {
            const Cell* cell = &world->cells[row_base + x];
            if (cell->colony_id == colony->id) {
                sum_x += (float)x;
                sum_y += (float)y;
                cells++;
            }
        }
    }

    if (cells > 0) {
        proto_colony->x = sum_x / (float)cells;
        proto_colony->y = sum_y / (float)cells;
    }
}

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
    server->world_width = world_width;
    server->world_height = world_height;
    server->default_colonies = DEFAULT_INITIAL_COLONY_COUNT;
    server->running = false;
    server->paused = false;
    server->tick_rate_ms = DEFAULT_TICK_RATE_MS;
    server->speed_multiplier = 1.0f;
    server->next_client_id = 1;
    
    return server;
}

Server* server_create_headless(int world_width, int world_height, int thread_count) {
    if (world_width <= 0 || world_height <= 0 || thread_count <= 0) {
        return NULL;
    }

    Server* server = (Server*)calloc(1, sizeof(Server));
    if (!server) return NULL;

    server->world = world_create(world_width, world_height);
    if (!server->world) {
        free(server);
        return NULL;
    }

    server->pool = threadpool_create(thread_count);
    if (!server->pool) {
        world_destroy(server->world);
        free(server);
        return NULL;
    }

    int regions = thread_count > 1 ? 4 : 2;
    server->parallel_ctx = parallel_create(server->pool, server->world, regions, regions);
    if (!server->parallel_ctx) {
        threadpool_destroy(server->pool);
        world_destroy(server->world);
        free(server);
        return NULL;
    }
    parallel_init_regions(server->parallel_ctx, world_width, world_height);

    server->atomic_world = atomic_world_create(server->world, server->pool, thread_count);
    if (!server->atomic_world) {
        parallel_destroy(server->parallel_ctx);
        threadpool_destroy(server->pool);
        world_destroy(server->world);
        free(server);
        return NULL;
    }

    if (pthread_mutex_init(&server->clients_mutex, NULL) != 0) {
        atomic_world_destroy(server->atomic_world);
        parallel_destroy(server->parallel_ctx);
        threadpool_destroy(server->pool);
        world_destroy(server->world);
        free(server);
        return NULL;
    }

    server->clients = NULL;
    server->client_count = 0;
    server->world_width = world_width;
    server->world_height = world_height;
    server->default_colonies = DEFAULT_INITIAL_COLONY_COUNT;
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

int server_build_protocol_world_snapshot(const World* world,
                                         bool paused,
                                         float speed_multiplier,
                                         ProtoWorld* proto_world) {
    if (!world || !proto_world) {
        return -1;
    }

    proto_world_init(proto_world);
    
    proto_world->width = (uint32_t)world->width;
    proto_world->height = (uint32_t)world->height;
    proto_world->tick = (uint32_t)world->tick;
    proto_world->paused = paused;
    proto_world->speed_multiplier = speed_multiplier;

    uint32_t count = 0;
    uint16_t proto_index_by_colony_id_stack[512];
    uint16_t* proto_index_by_colony_id = NULL;
    size_t proto_index_by_colony_id_capacity = 0;
    float sum_x[MAX_COLONIES] = {0};
    float sum_y[MAX_COLONIES] = {0};
    uint32_t sample_count[MAX_COLONIES] = {0};

    if (world->colony_index_capacity > 0) {
        proto_index_by_colony_id_capacity = world->colony_index_capacity;
        if (proto_index_by_colony_id_capacity <=
            (sizeof(proto_index_by_colony_id_stack) / sizeof(proto_index_by_colony_id_stack[0]))) {
            proto_index_by_colony_id = proto_index_by_colony_id_stack;
        } else {
            proto_index_by_colony_id = (uint16_t*)malloc(proto_index_by_colony_id_capacity *
                                                         sizeof(uint16_t));
        }

        if (!proto_index_by_colony_id) {
            return -1;
        }

        memset(proto_index_by_colony_id,
               0xFF,
               proto_index_by_colony_id_capacity * sizeof(uint16_t));
    }

    for (size_t i = 0; i < world->colony_count && count < MAX_COLONIES; i++) {
        const Colony* colony = &world->colonies[i];
        if (!colony->active) {
            continue;
        }

        ProtoColony* proto_colony = &proto_world->colonies[count];
        proto_colony->id = colony->id;
        copy_colony_name(proto_colony->name, colony->name);
        proto_colony->population = (uint32_t)colony->cell_count;
        proto_colony->max_population = (uint32_t)colony->max_cell_count;
        proto_colony->growth_rate = colony->genome.spread_rate;
        proto_colony->color_r = colony->color.r;
        proto_colony->color_g = colony->color.g;
        proto_colony->color_b = colony->color.b;
        proto_colony->alive = colony->active;
        proto_colony->shape_seed = colony->shape_seed;
        proto_colony->wobble_phase = colony->wobble_phase;
        proto_colony->shape_evolution = colony->shape_evolution;
        proto_colony->x = 0.0f;
        proto_colony->y = 0.0f;
        proto_colony->radius = 0.0f;

        if (proto_index_by_colony_id && (size_t)colony->id < proto_index_by_colony_id_capacity) {
            proto_index_by_colony_id[colony->id] = (uint16_t)count;
        }
        count++;
    }
    
    // Build grid data from world cells for smaller snapshots and always
    // accumulate centroid data in the same pass.
    uint32_t grid_size = proto_world->width * proto_world->height;
    bool inline_grid = (grid_size > 0 && grid_size <= MAX_INLINE_GRID_SIZE);
    if (inline_grid) {
        proto_world->grid = (uint16_t*)malloc((size_t)grid_size * sizeof(uint16_t));
        if (!proto_world->grid) {
            if (proto_index_by_colony_id != proto_index_by_colony_id_stack) {
                free(proto_index_by_colony_id);
            }
            proto_world_free(proto_world);
            return -1;
        }
        proto_world->grid_size = grid_size;
        proto_world->has_grid = true;
    }

    for (int y = 0; y < world->height; y++) {
        int row_base = y * world->width;
        for (int x = 0; x < world->width; x++) {
            int idx = row_base + x;
            const Cell* cell = &world->cells[idx];
            uint32_t colony_id = cell->colony_id;
            if (proto_world->grid) {
                proto_world->grid[idx] = (uint16_t)colony_id;
            }

            if (colony_id == 0 || !proto_index_by_colony_id ||
                (size_t)colony_id >= proto_index_by_colony_id_capacity) {
                continue;
            }

            uint16_t proto_index = proto_index_by_colony_id[colony_id];
            if (proto_index == UINT16_MAX || proto_index >= count) {
                continue;
            }

            sum_x[proto_index] += (float)x;
            sum_y[proto_index] += (float)y;
            sample_count[proto_index]++;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        ProtoColony* proto_colony = &proto_world->colonies[i];
        if (sample_count[i] > 0) {
            proto_colony->x = sum_x[i] / (float)sample_count[i];
            proto_colony->y = sum_y[i] / (float)sample_count[i];
        }
        proto_colony->radius = proto_colony->population > 0 ? sqrtf((float)proto_colony->population / 3.14159f) : 0.0f;
    }

    proto_world->colony_count = count;
    if (proto_index_by_colony_id != proto_index_by_colony_id_stack) {
        free(proto_index_by_colony_id);
    }
    return 0;
}

// Convert internal World/Colony to protocol ProtoWorld for serialization
static int build_protocol_world(Server* server, ProtoWorld* proto_world) {
    return server_build_protocol_world_snapshot(server->world,
                                                server->paused,
                                                server->speed_multiplier,
                                                proto_world);
}

void server_broadcast_world_state(Server* server) {
    if (!server) return;
    
    // Build protocol world state
    ProtoWorld proto_world;
    if (build_protocol_world(server, &proto_world) < 0) {
        return;
    }
    
    // Serialize
    uint8_t* buffer = NULL;
    size_t len = 0;
    if (protocol_serialize_world_state(&proto_world, &buffer, &len) < 0) {
        proto_world_free(&proto_world);
        return;
    }

    uint32_t grid_size = (uint32_t)(server->world->width * server->world->height);
    size_t chunk_count = 0;
    uint8_t** chunk_buffers = NULL;
    size_t* chunk_lengths = NULL;
    uint16_t* chunk_cells = NULL;

    if (!proto_world.has_grid && grid_size > 0 && grid_size <= MAX_GRID_SIZE) {
        chunk_count = (grid_size + MAX_GRID_CHUNK_CELLS - 1u) / MAX_GRID_CHUNK_CELLS;
        chunk_buffers = (uint8_t**)calloc(chunk_count, sizeof(uint8_t*));
        chunk_lengths = (size_t*)calloc(chunk_count, sizeof(size_t));
        chunk_cells = (uint16_t*)malloc((size_t)MAX_GRID_CHUNK_CELLS * sizeof(uint16_t));
        if (!chunk_buffers || !chunk_lengths || !chunk_cells) {
            free(chunk_buffers);
            free(chunk_lengths);
            free(chunk_cells);
            free(buffer);
            proto_world_free(&proto_world);
            return;
        }

        for (size_t chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
            uint32_t start_index = (uint32_t)(chunk_idx * MAX_GRID_CHUNK_CELLS);
            uint32_t cell_count = grid_size - start_index;
            if (cell_count > MAX_GRID_CHUNK_CELLS) {
                cell_count = MAX_GRID_CHUNK_CELLS;
            }

            for (uint32_t i = 0; i < cell_count; i++) {
                chunk_cells[i] = (uint16_t)server->world->cells[start_index + i].colony_id;
            }

            ProtoWorldDeltaGridChunk chunk = {
                .tick = proto_world.tick,
                .width = proto_world.width,
                .height = proto_world.height,
                .total_cells = grid_size,
                .start_index = start_index,
                .cell_count = cell_count,
                .final_chunk = (chunk_idx + 1u == chunk_count),
                .cells = chunk_cells,
            };

            if (protocol_serialize_world_delta_grid_chunk(&chunk, &chunk_buffers[chunk_idx], &chunk_lengths[chunk_idx]) < 0) {
                for (size_t free_idx = 0; free_idx < chunk_count; free_idx++) {
                    free(chunk_buffers[free_idx]);
                }
                free(chunk_buffers);
                free(chunk_lengths);
                free(chunk_cells);
                free(buffer);
                proto_world_free(&proto_world);
                return;
            }
        }
    }
    free(chunk_cells);
    
    // Broadcast to all clients
    pthread_mutex_lock(&server->clients_mutex);
    ClientSession* client = server->clients;
    ClientSession* prev = NULL;
    
    while (client) {
        ClientSession* next = client->next;
        
        if (client->active && client->socket && client->socket->connected) {
            int result = protocol_send_message(client->socket->fd, MSG_WORLD_STATE, buffer, len);
            if (result == 0) {
                for (size_t chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
                    result = protocol_send_message(client->socket->fd, MSG_WORLD_DELTA,
                                                   chunk_buffers[chunk_idx], chunk_lengths[chunk_idx]);
                    if (result < 0) {
                        break;
                    }
                }
            }
            if (result == 0 && client->selected_colony != 0) {
                server_send_colony_info(server, client, client->selected_colony);
            }
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

    for (size_t chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
        free(chunk_buffers[chunk_idx]);
    }
    free(chunk_buffers);
    free(chunk_lengths);
    
    free(buffer);
    proto_world_free(&proto_world);
}

void server_send_colony_info(Server* server, ClientSession* client, uint32_t colony_id) {
    if (!server || !client || !client->socket || colony_id == 0) return;

    ProtoColonyDetail detail;
    memset(&detail, 0, sizeof(detail));
    detail.base.id = colony_id;
    detail.tick = (uint32_t)server->world->tick;
    
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
    
    if (found) {
        Colony* colony = &server->world->colonies[colony_idx];
        fill_proto_colony_detail_base(server->world, colony, &detail.base);
        detail.age = colony->age > UINT32_MAX ? UINT32_MAX : (uint32_t)colony->age;
        detail.parent_id = colony->parent_id;
        detail.state = (uint8_t)colony->state;
        if (colony->is_dormant) {
            detail.flags |= COLONY_DETAIL_FLAG_DORMANT;
        }
        detail.stress_level = colony->stress_level;
        detail.biofilm_strength = colony->biofilm_strength;
        detail.signal_strength = colony->signal_strength;
        detail.drift_x = colony->drift_x;
        detail.drift_y = colony->drift_y;
        detail.behavior_mode = (uint8_t)colony->behavior_mode;
        detail.focus_direction = colony->focus_direction;
        detail.dominant_sensor = colony->dominant_sensor;
        detail.dominant_drive = colony->dominant_drive;
        detail.secondary_sensor = colony->secondary_sensor;
        detail.secondary_drive = colony->secondary_drive;
        detail.dominant_sensor_value = colony->dominant_sensor_value;
        detail.dominant_drive_value = colony->dominant_drive_value;
        detail.secondary_sensor_value = colony->secondary_sensor_value;
        detail.secondary_drive_value = colony->secondary_drive_value;
        detail.action_expand = colony->behavior_actions[COLONY_ACTION_EXPAND];
        detail.action_attack = colony->behavior_actions[COLONY_ACTION_ATTACK];
        detail.action_defend = colony->behavior_actions[COLONY_ACTION_DEFEND];
        detail.action_signal = colony->behavior_actions[COLONY_ACTION_SIGNAL];
        detail.action_transfer = colony->behavior_actions[COLONY_ACTION_TRANSFER];
        detail.action_dormancy = colony->behavior_actions[COLONY_ACTION_DORMANCY];
        detail.action_motility = colony->behavior_actions[COLONY_ACTION_MOTILITY];
        fill_proto_colony_graph_links(colony, &detail);
        detail.trait_expansion = summarize_trait_expansion(&colony->genome);
        detail.trait_aggression = summarize_trait_aggression(&colony->genome);
        detail.trait_resilience = summarize_trait_resilience(&colony->genome);
        detail.trait_cooperation = summarize_trait_cooperation(&colony->genome);
        detail.trait_efficiency = summarize_trait_efficiency(&colony->genome);
        detail.trait_learning = summarize_trait_learning(&colony->genome);
    }

    uint8_t buffer[COLONY_DETAIL_SERIALIZED_SIZE];
    int len = protocol_serialize_colony_detail(&detail, buffer);
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
            // Reset world while keeping the previous world alive until the new
            // one and its atomic wrapper are ready.
            {
                World* new_world = world_create(server->world_width, server->world_height);
                AtomicWorld* new_atomic_world = NULL;
                if (new_world) {
                    world_init_random_colonies(new_world, server->default_colonies);
                    new_atomic_world = atomic_world_create(new_world, server->pool, server->pool->thread_count);
                }

                if (!new_world || !new_atomic_world) {
                    atomic_world_destroy(new_atomic_world);
                    world_destroy(new_world);
                    printf("World reset failed for client %u\n", client->id);
                    break;
                }

                atomic_world_destroy(server->atomic_world);
                world_destroy(server->world);
                server->world = new_world;
                server->atomic_world = new_atomic_world;
                if (server->parallel_ctx) {
                    server->parallel_ctx->world = server->world;
                    parallel_init_regions(server->parallel_ctx, server->world_width, server->world_height);
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
                ProtoCommandStatus status;
                MessageType reply_type = server_spawn_colony(server, spawn, &status);
                server_send_command_status(server, client, reply_type, &status);
                if (reply_type == MSG_ACK) {
                    printf("Spawned colony %u at (%.1f, %.1f) for client %u\n",
                           status.entity_id, spawn->x, spawn->y, client->id);
                } else {
                    printf("Rejected spawn colony request at (%.1f, %.1f) for client %u: %s\n",
                           spawn->x, spawn->y, client->id, status.message);
                }
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
