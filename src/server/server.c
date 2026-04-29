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
#include <stddef.h>

// Protocol types - we need to include protocol.h but types.h already defined World/Colony
// So we include protocol.h here and use the types directly knowing they come from types.h (via world.h)
// The protocol functions work with the protocol-defined structures, so we'll build them inline
#include "../shared/protocol.h"

// Forward declarations for thread functions
static void* accept_thread_func(void* arg);
static void* simulation_thread_func(void* arg);

#define SERVER_CLIENT_RECV_BUDGET_MESSAGES 32

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

static void client_baseline_entry_free(ClientBaselineEntry* entry) {
    if (!entry) return;
    free(entry->grid);
    memset(entry, 0, sizeof(*entry));
}

static void client_send_batch_free(ClientSendBatch* batch) {
    if (!batch) return;

    for (size_t i = 0; i < batch->frame_count; i++) {
        free(batch->frames[i].data);
    }
    free(batch->frames);
    client_baseline_entry_free(&batch->baseline_candidate);
    memset(batch, 0, sizeof(*batch));
}

static bool client_send_batch_has_frames(const ClientSendBatch* batch) {
    return batch && batch->frame_count > 0;
}

static size_t client_send_batch_remaining_frames(const ClientSendBatch* batch) {
    if (!client_send_batch_has_frames(batch) || batch->frame_index >= batch->frame_count) {
        return 0;
    }
    return batch->frame_count - batch->frame_index;
}

static uint32_t transport_tick_lag(uint32_t newer, uint32_t older) {
    return newer >= older ? newer - older : 0;
}

static void client_transport_refresh_freshness(ClientSession* client) {
    if (!client) return;

    ClientTransportStats* stats = &client->transport;
    stats->queued_to_started_lag_ticks =
        (stats->has_last_queued_world && stats->has_last_started_world)
            ? transport_tick_lag(stats->last_queued_world_tick, stats->last_started_world_tick)
            : 0;
    stats->queued_to_acked_lag_ticks =
        (stats->has_last_queued_world && stats->has_last_acked_world)
            ? transport_tick_lag(stats->last_queued_world_tick, stats->last_acked_world_tick)
            : 0;
}

static void client_transport_refresh_queue_depth(ClientSession* client) {
    if (!client) return;

    size_t depth = client_send_batch_remaining_frames(&client->send_current) +
                   client_send_batch_remaining_frames(&client->send_pending);
    client->transport.queue_depth_frames = depth;
    if (depth > client->transport.max_queue_depth_frames) {
        client->transport.max_queue_depth_frames = depth;
    }
}

static uint32_t client_send_batch_world_tick(const ClientSendBatch* batch) {
    if (batch && batch->has_world_tick) {
        return batch->world_tick;
    }
    if (!batch || !batch->baseline_candidate.occupied) {
        return 0;
    }
    return batch->baseline_candidate.tick;
}

static void client_transport_record_world_queued(ClientSession* client, const ClientSendBatch* batch) {
    if (!client || !batch || !batch->has_world_sequence) return;

    ClientTransportStats* stats = &client->transport;
    stats->world_batches_queued++;
    stats->has_last_queued_world = true;
    stats->last_queued_world_sequence = batch->world_sequence;
    stats->last_queued_world_tick = client_send_batch_world_tick(batch);
    client_transport_refresh_freshness(client);
}

static void client_transport_record_world_replaced(ClientSession* client,
                                                   const ClientSendBatch* batch,
                                                   bool pending_batch) {
    if (!client || !batch || !batch->has_world_sequence) return;

    client->transport.world_batches_replaced++;
    if (pending_batch) {
        client->transport.pending_world_batches_replaced++;
    } else {
        client->transport.unsent_world_batches_replaced++;
    }
}

static void client_transport_record_world_started(ClientSession* client, const ClientSendBatch* batch) {
    if (!client || !batch || !batch->has_world_sequence) return;

    ClientTransportStats* stats = &client->transport;
    stats->world_batches_started++;
    stats->has_last_started_world = true;
    stats->last_started_world_sequence = batch->world_sequence;
    stats->last_started_world_tick = client_send_batch_world_tick(batch);
    client_transport_refresh_freshness(client);
}

static void client_transport_record_world_completed(ClientSession* client, const ClientSendBatch* batch) {
    if (!client || !batch || !batch->has_world_sequence) return;

    ClientTransportStats* stats = &client->transport;
    stats->world_batches_completed++;
    stats->has_last_completed_world = true;
    stats->last_completed_world_sequence = batch->world_sequence;
    stats->last_completed_world_tick = client_send_batch_world_tick(batch);
}

static void client_transport_reset_freshness(ClientSession* client) {
    if (!client) return;

    ClientTransportStats* stats = &client->transport;
    stats->has_last_queued_world = false;
    stats->last_queued_world_sequence = 0;
    stats->last_queued_world_tick = 0;
    stats->has_last_started_world = false;
    stats->last_started_world_sequence = 0;
    stats->last_started_world_tick = 0;
    stats->has_last_completed_world = false;
    stats->last_completed_world_sequence = 0;
    stats->last_completed_world_tick = 0;
    stats->has_last_acked_world = false;
    stats->last_acked_world_sequence = 0;
    stats->last_acked_world_tick = 0;
    stats->queued_to_started_lag_ticks = 0;
    stats->queued_to_acked_lag_ticks = 0;
}

static void client_reset_transport_for_world_reset(ClientSession* client) {
    if (!client) return;

    client_send_batch_free(&client->send_current);
    client_send_batch_free(&client->send_pending);
    for (size_t i = 0; i < CLIENT_BASELINE_RING_SIZE; i++) {
        client_baseline_entry_free(&client->baseline_ring[i]);
    }
    client->baseline_next = 0;
    protocol_ack_window_init(&client->world_ack_window);
    client_transport_reset_freshness(client);
    client_transport_refresh_queue_depth(client);
}

static void server_reset_client_transport_for_world_reset(Server* server) {
    if (!server) return;

    pthread_mutex_lock(&server->clients_mutex);
    ClientSession* client = server->clients;
    while (client) {
        client_reset_transport_for_world_reset(client);
        client = client->next;
    }
    pthread_mutex_unlock(&server->clients_mutex);
}

static int client_send_batch_reserve(ClientSendBatch* batch, size_t frame_count) {
    if (!batch) return -1;
    if (frame_count <= batch->frame_capacity) return 0;

    size_t next_capacity = batch->frame_capacity ? batch->frame_capacity : 4;
    while (next_capacity < frame_count) {
        next_capacity *= 2;
    }

    ClientFrame* frames = (ClientFrame*)realloc(batch->frames, next_capacity * sizeof(ClientFrame));
    if (!frames) return -1;

    memset(frames + batch->frame_capacity, 0,
           (next_capacity - batch->frame_capacity) * sizeof(ClientFrame));
    batch->frames = frames;
    batch->frame_capacity = next_capacity;
    return 0;
}

static int client_send_batch_add_message(ClientSendBatch* batch,
                                         MessageType type,
                                         const uint8_t* payload,
                                         size_t len) {
    if (!batch) return -1;

    uint8_t* frame = NULL;
    size_t frame_len = 0;
    if (protocol_build_message(type, payload, len, &frame, &frame_len) < 0) {
        return -1;
    }

    if (client_send_batch_reserve(batch, batch->frame_count + 1) < 0) {
        free(frame);
        return -1;
    }

    batch->frames[batch->frame_count].data = frame;
    batch->frames[batch->frame_count].len = frame_len;
    batch->frame_count++;
    if (type == MSG_WORLD_STATE) {
        MessageHeader header;
        if (protocol_deserialize_header(frame, &header) == MESSAGE_HEADER_SIZE) {
            batch->has_world_sequence = true;
            batch->world_sequence = header.sequence;
        }
    }
    return 0;
}

static void client_send_batch_attach_world_baseline(ClientSendBatch* batch, const World* world, uint32_t tick) {
    if (!batch || !batch->has_world_sequence) return;

    batch->has_world_tick = true;
    batch->world_tick = tick;
    if (!world || !world->cells) return;

    uint32_t grid_size = (uint32_t)(world->width * world->height);
    if (grid_size == 0 || grid_size > MAX_GRID_SIZE) return;

    uint16_t* grid = (uint16_t*)malloc((size_t)grid_size * sizeof(uint16_t));
    if (!grid) return;

    for (uint32_t i = 0; i < grid_size; i++) {
        grid[i] = (uint16_t)world->cells[i].colony_id;
    }

    client_baseline_entry_free(&batch->baseline_candidate);
    batch->baseline_candidate.occupied = true;
    batch->baseline_candidate.sent = false;
    batch->baseline_candidate.acked = false;
    batch->baseline_candidate.sequence = batch->world_sequence;
    batch->baseline_candidate.tick = tick;
    batch->baseline_candidate.width = (uint32_t)world->width;
    batch->baseline_candidate.height = (uint32_t)world->height;
    batch->baseline_candidate.grid_size = grid_size;
    batch->baseline_candidate.bytes = (size_t)grid_size * sizeof(uint16_t);
    batch->baseline_candidate.grid = grid;
}

static void client_baseline_ring_store(ClientSession* client, ClientBaselineEntry* candidate) {
    if (!client || !candidate || !candidate->occupied) return;

    size_t slot = client->baseline_next % CLIENT_BASELINE_RING_SIZE;
    client_baseline_entry_free(&client->baseline_ring[slot]);
    candidate->sent = true;
    client->baseline_ring[slot] = *candidate;
    memset(candidate, 0, sizeof(*candidate));
    client->baseline_next = (slot + 1u) % CLIENT_BASELINE_RING_SIZE;
}

static const ClientBaselineEntry* client_baseline_ring_find_delta_parent(const ClientSession* client,
                                                                         uint32_t width,
                                                                         uint32_t height,
                                                                         uint32_t grid_size) {
    if (!client) return NULL;
    if (!client->transport.has_last_completed_world ||
        !client->transport.has_last_acked_world ||
        client->transport.last_completed_world_sequence != client->transport.last_acked_world_sequence) {
        return NULL;
    }

    for (size_t i = 0; i < CLIENT_BASELINE_RING_SIZE; i++) {
        const ClientBaselineEntry* entry = &client->baseline_ring[i];
        if (!entry->occupied || !entry->acked || !entry->grid) {
            continue;
        }
        if (entry->width != width || entry->height != height || entry->grid_size != grid_size) {
            continue;
        }
        if (entry->sequence == client->transport.last_acked_world_sequence) {
            return entry;
        }
    }
    return NULL;
}

static int client_build_world_delta_patch(const ClientSession* client,
                                          const World* world,
                                          uint32_t tick,
                                          uint8_t** buffer,
                                          size_t* len) {
    if (!client || !world || !world->cells || !buffer || !len) return -1;

    uint32_t grid_size = (uint32_t)(world->width * world->height);
    const ClientBaselineEntry* parent =
        client_baseline_ring_find_delta_parent(client,
                                               (uint32_t)world->width,
                                               (uint32_t)world->height,
                                               grid_size);
    if (!parent) {
        return -1;
    }

    return protocol_serialize_world_delta_grid_patch_from_u32_field(tick,
                                                                    (uint32_t)world->width,
                                                                    (uint32_t)world->height,
                                                                    grid_size,
                                                                    parent->sequence,
                                                                    parent->grid,
                                                                    world->cells,
                                                                    sizeof(Cell),
                                                                    offsetof(Cell, colony_id),
                                                                    buffer,
                                                                    len);
}

static bool transport_frame_bytes_add(size_t payload_len, size_t* total) {
    if (!total || payload_len > SIZE_MAX - MESSAGE_HEADER_SIZE ||
        *total > SIZE_MAX - MESSAGE_HEADER_SIZE - payload_len) {
        return false;
    }
    *total += MESSAGE_HEADER_SIZE + payload_len;
    return true;
}

static bool world_delta_patch_is_cheaper(size_t delta_parent_len,
                                         size_t patch_len,
                                         size_t fallback_world_len,
                                         const size_t* chunk_lengths,
                                         size_t chunk_count) {
    size_t patch_bytes = 0;
    size_t fallback_bytes = 0;
    if (!transport_frame_bytes_add(delta_parent_len, &patch_bytes) ||
        !transport_frame_bytes_add(patch_len, &patch_bytes) ||
        !transport_frame_bytes_add(fallback_world_len, &fallback_bytes)) {
        return false;
    }

    for (size_t i = 0; i < chunk_count; i++) {
        if (!transport_frame_bytes_add(chunk_lengths[i], &fallback_bytes)) {
            return false;
        }
    }

    return patch_bytes < fallback_bytes;
}

static void client_baseline_ring_mark_acked(ClientSession* client) {
    if (!client) return;

    for (size_t i = 0; i < CLIENT_BASELINE_RING_SIZE; i++) {
        ClientBaselineEntry* entry = &client->baseline_ring[i];
        if (entry->occupied &&
            !entry->acked &&
            protocol_ack_window_contains(&client->world_ack_window, entry->sequence)) {
            entry->acked = true;
            client->transport.baselines_acked++;
            if (!client->transport.has_last_acked_world ||
                entry->tick >= client->transport.last_acked_world_tick) {
                client->transport.has_last_acked_world = true;
                client->transport.last_acked_world_sequence = entry->sequence;
                client->transport.last_acked_world_tick = entry->tick;
                client_transport_refresh_freshness(client);
            }
        }
    }
}

static void client_handle_world_ack(ClientSession* client, const ProtoAckPayload* ack) {
    if (!client || !ack || ack->channel != PROTO_ACK_CHANNEL_WORLD_UPDATE) return;

    client->transport.ack_messages_received++;
    protocol_ack_window_record(&client->world_ack_window, ack->latest_sequence);
    for (uint32_t bit = 0; bit < 32u; bit++) {
        if ((ack->ack_bits & (1u << bit)) != 0) {
            protocol_ack_window_record(&client->world_ack_window,
                                       ack->latest_sequence - (bit + 1u));
        }
    }
    client_baseline_ring_mark_acked(client);
}

static void client_send_batch_move(ClientSendBatch* dst, ClientSendBatch* src) {
    if (!dst || !src) return;
    *dst = *src;
    memset(src, 0, sizeof(*src));
}

static void client_queue_send_batch(ClientSession* client, ClientSendBatch* batch, bool coalesce_unsent_current) {
    if (!client || !client_send_batch_has_frames(batch)) return;

    client_transport_record_world_queued(client, batch);
    if (!client_send_batch_has_frames(&client->send_current)) {
        client_send_batch_move(&client->send_current, batch);
        client_transport_refresh_queue_depth(client);
        return;
    }

    if (coalesce_unsent_current && !client->send_current.started) {
        client_transport_record_world_replaced(client, &client->send_current, false);
        client_transport_record_world_replaced(client, &client->send_pending, true);
        client_send_batch_free(&client->send_current);
        client_send_batch_free(&client->send_pending);
        client_send_batch_move(&client->send_current, batch);
        client_transport_refresh_queue_depth(client);
        return;
    }

    client_transport_record_world_replaced(client, &client->send_pending, true);
    client_send_batch_free(&client->send_pending);
    client_send_batch_move(&client->send_pending, batch);
    client_transport_refresh_queue_depth(client);
}

static void server_free_client_session(ClientSession* client) {
    if (!client) return;

    if (client->socket) {
        net_socket_close(client->socket);
        client->socket = NULL;
    }
    protocol_recv_state_free(&client->recv_state);
    client_send_batch_free(&client->send_current);
    client_send_batch_free(&client->send_pending);
    for (size_t i = 0; i < CLIENT_BASELINE_RING_SIZE; i++) {
        client_baseline_entry_free(&client->baseline_ring[i]);
    }
    free(client);
}

static void server_cleanup_inactive_clients(Server* server) {
    if (!server) return;

    pthread_mutex_lock(&server->clients_mutex);
    ClientSession** link = &server->clients;
    while (*link) {
        ClientSession* client = *link;
        bool connected = client->socket && client->socket->connected;
        if (!client->active || !connected) {
            *link = client->next;
            server->client_count--;
            server_free_client_session(client);
            continue;
        }
        link = &client->next;
    }
    pthread_mutex_unlock(&server->clients_mutex);
}

static int server_collect_active_clients(Server* server, ClientSession*** clients, size_t* count) {
    if (!server || !clients || !count) return -1;

    *clients = NULL;
    *count = 0;

    pthread_mutex_lock(&server->clients_mutex);
    int capacity = server->client_count;
    if (capacity <= 0) {
        pthread_mutex_unlock(&server->clients_mutex);
        return 0;
    }

    ClientSession** list = (ClientSession**)malloc((size_t)capacity * sizeof(ClientSession*));
    if (!list) {
        pthread_mutex_unlock(&server->clients_mutex);
        return -1;
    }

    ClientSession* client = server->clients;
    while (client && *count < (size_t)capacity) {
        if (client->active && client->socket && client->socket->connected) {
            list[*count] = client;
            (*count)++;
        }
        client = client->next;
    }
    pthread_mutex_unlock(&server->clients_mutex);

    *clients = list;
    return 0;
}

static void server_mark_client_disconnected(ClientSession* client) {
    if (!client) return;
    client->active = false;
    if (client->socket) {
        client->socket->connected = false;
    }
}

static void server_pump_client_outboxes(Server* server) {
    if (!server) return;

    ClientSession** clients = NULL;
    size_t client_count = 0;
    if (server_collect_active_clients(server, &clients, &client_count) < 0) {
        return;
    }

    for (size_t client_idx = 0; client_idx < client_count; client_idx++) {
        ClientSession* client = clients[client_idx];
        if (!client || !client->active || !client->socket || !client->socket->connected) {
            continue;
        }

        while (client_send_batch_has_frames(&client->send_current) && client->active) {
            if (client->send_current.frame_index >= client->send_current.frame_count) {
                client_send_batch_free(&client->send_current);
                if (client_send_batch_has_frames(&client->send_pending)) {
                    client_send_batch_move(&client->send_current, &client->send_pending);
                    client_transport_refresh_queue_depth(client);
                    continue;
                }
                client_transport_refresh_queue_depth(client);
                break;
            }

            ClientFrame* frame = &client->send_current.frames[client->send_current.frame_index];
            size_t before = client->send_current.frame_offset;
            bool was_started = client->send_current.started;
            int result = protocol_send_frame_nonblocking(client->socket->fd,
                                                         frame->data,
                                                         frame->len,
                                                         &client->send_current.frame_offset);
            if (client->send_current.frame_offset > before) {
                client->transport.bytes_sent += client->send_current.frame_offset - before;
                client->send_current.started = true;
                if (!was_started && client->send_current.baseline_candidate.occupied) {
                    client_transport_record_world_started(client, &client->send_current);
                    client_baseline_ring_store(client, &client->send_current.baseline_candidate);
                } else if (!was_started) {
                    client_transport_record_world_started(client, &client->send_current);
                }
            }

            if (result < 0) {
                client->transport.send_error_events++;
                printf("Client %u disconnected\n", client->id);
                server_mark_client_disconnected(client);
                break;
            }
            if (result == 0) {
                client->transport.send_backpressure_events++;
                client_transport_refresh_queue_depth(client);
                break;
            }

            bool batch_completed = client->send_current.frame_index + 1u >= client->send_current.frame_count;
            client->transport.frames_sent++;
            if (batch_completed) {
                client_transport_record_world_completed(client, &client->send_current);
            }
            free(frame->data);
            frame->data = NULL;
            frame->len = 0;
            client->send_current.frame_index++;
            client->send_current.frame_offset = 0;
            client_transport_refresh_queue_depth(client);
        }

        if (client_send_batch_has_frames(&client->send_current) &&
            client->send_current.frame_index >= client->send_current.frame_count) {
            client_send_batch_free(&client->send_current);
            if (client_send_batch_has_frames(&client->send_pending)) {
                client_send_batch_move(&client->send_current, &client->send_pending);
            }
            client_transport_refresh_queue_depth(client);
        }
    }

    free(clients);
    server_cleanup_inactive_clients(server);
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
        server_free_client_session(client);
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
    uint32_t* proto_index_by_world_index = NULL;
    float sum_x[MAX_COLONIES] = {0};
    float sum_y[MAX_COLONIES] = {0};
    uint32_t sample_count[MAX_COLONIES] = {0};

    if (world->colony_count > 0) {
        proto_index_by_world_index = (uint32_t*)malloc(world->colony_count * sizeof(uint32_t));
        if (!proto_index_by_world_index) {
            return -1;
        }
        for (size_t i = 0; i < world->colony_count; i++) {
            proto_index_by_world_index[i] = UINT32_MAX;
        }
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

        if (proto_index_by_world_index) {
            proto_index_by_world_index[i] = count;
        }
        count++;
    }
    
    // Build grid data from world cells for smaller snapshots and always
    // accumulate centroid data in the same pass.
    uint32_t grid_size = proto_world->width * proto_world->height;
    bool inline_grid = (grid_size > 0 && grid_size <= MAX_INLINE_GRID_SIZE);
    if (inline_grid) {
        proto_world_alloc_grid(proto_world, proto_world->width, proto_world->height);
        if (!proto_world->grid) {
            free(proto_index_by_world_index);
            proto_world_free(proto_world);
            return -1;
        }
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

            if (colony_id == 0 || !proto_index_by_world_index || (size_t)colony_id >= world->colony_index_capacity) {
                continue;
            }

            uint32_t world_index = world->colony_index_map[colony_id];
            if (world_index == UINT32_MAX || world_index >= world->colony_count) {
                continue;
            }

            uint32_t proto_index = proto_index_by_world_index[world_index];
            if (proto_index == UINT32_MAX || proto_index >= count) {
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
    free(proto_index_by_world_index);
    return 0;
}

// Convert internal World/Colony to protocol ProtoWorld for serialization
static int build_protocol_world(Server* server, ProtoWorld* proto_world) {
    return server_build_protocol_world_snapshot(server->world,
                                                server->paused,
                                                server->speed_multiplier,
                                                proto_world);
}

static int build_colony_info_payload(Server* server, uint32_t colony_id, uint8_t** out_buffer, size_t* out_len) {
    if (!server || colony_id == 0 || !out_buffer || !out_len) return -1;

    *out_buffer = NULL;
    *out_len = 0;

    ProtoColonyDetail detail;
    memset(&detail, 0, sizeof(detail));
    detail.base.id = colony_id;
    detail.tick = (uint32_t)server->world->tick;

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

    uint8_t* buffer = (uint8_t*)malloc(COLONY_DETAIL_SERIALIZED_SIZE);
    if (!buffer) return -1;

    int len = protocol_serialize_colony_detail(&detail, buffer);
    if (len <= 0) {
        free(buffer);
        return -1;
    }

    *out_buffer = buffer;
    *out_len = (size_t)len;
    return 0;
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

    uint8_t* delta_parent_buffer = NULL;
    size_t delta_parent_len = 0;
    ProtoWorld delta_parent_world = proto_world;
    delta_parent_world.grid = NULL;
    delta_parent_world.grid_size = 0;
    delta_parent_world.has_grid = false;
    protocol_serialize_world_state(&delta_parent_world, &delta_parent_buffer, &delta_parent_len);

    uint32_t grid_size = (uint32_t)(server->world->width * server->world->height);
    size_t chunk_count = 0;
    uint8_t** chunk_buffers = NULL;
    size_t* chunk_lengths = NULL;

    if (!proto_world.has_grid && grid_size > 0 && grid_size <= MAX_GRID_SIZE) {
        chunk_count = (grid_size + MAX_GRID_CHUNK_CELLS - 1u) / MAX_GRID_CHUNK_CELLS;
        chunk_buffers = (uint8_t**)calloc(chunk_count, sizeof(uint8_t*));
        chunk_lengths = (size_t*)calloc(chunk_count, sizeof(size_t));
        if (!chunk_buffers || !chunk_lengths) {
            free(chunk_buffers);
            free(chunk_lengths);
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

            if (protocol_serialize_world_delta_grid_chunk_from_u32_field(proto_world.tick,
                                                                         proto_world.width,
                                                                         proto_world.height,
                                                                         grid_size,
                                                                         start_index,
                                                                         cell_count,
                                                                         (chunk_idx + 1u == chunk_count),
                                                                         server->world->cells,
                                                                         sizeof(Cell),
                                                                         offsetof(Cell, colony_id),
                                                                         &chunk_buffers[chunk_idx],
                                                                         &chunk_lengths[chunk_idx]) < 0) {
                for (size_t free_idx = 0; free_idx < chunk_count; free_idx++) {
                    free(chunk_buffers[free_idx]);
                }
                free(chunk_buffers);
                free(chunk_lengths);
                free(buffer);
                proto_world_free(&proto_world);
                return;
            }
        }
    }
    
    // Queue ordered world-update batches for each client, then flush sockets
    // outside the client list lock.
    pthread_mutex_lock(&server->clients_mutex);
    ClientSession* client = server->clients;

    while (client) {
        if (client->active && client->socket && client->socket->connected) {
            ClientSendBatch batch = {0};
            uint8_t* patch_buffer = NULL;
            size_t patch_len = 0;
            bool use_patch = delta_parent_buffer &&
                             client_build_world_delta_patch(client,
                                                            server->world,
                                                            proto_world.tick,
                                                            &patch_buffer,
                                                            &patch_len) == 0 &&
                             world_delta_patch_is_cheaper(delta_parent_len,
                                                          patch_len,
                                                          len,
                                                          chunk_lengths,
                                                          chunk_count);
            int result = client_send_batch_add_message(&batch,
                                                       MSG_WORLD_STATE,
                                                       use_patch ? delta_parent_buffer : buffer,
                                                       use_patch ? delta_parent_len : len);
            if (result == 0 && use_patch) {
                result = client_send_batch_add_message(&batch, MSG_WORLD_DELTA, patch_buffer, patch_len);
            }
            if (result == 0 && !use_patch) {
                for (size_t chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
                    result = client_send_batch_add_message(&batch,
                                                           MSG_WORLD_DELTA,
                                                           chunk_buffers[chunk_idx],
                                                           chunk_lengths[chunk_idx]);
                    if (result < 0) {
                        break;
                    }
                }
            }
            if (result == 0 && client->selected_colony != 0) {
                uint8_t* detail_buffer = NULL;
                size_t detail_len = 0;
                if (build_colony_info_payload(server, client->selected_colony, &detail_buffer, &detail_len) == 0) {
                    result = client_send_batch_add_message(&batch, MSG_COLONY_INFO, detail_buffer, detail_len);
                    free(detail_buffer);
                } else {
                    result = -1;
                }
            }

            if (result == 0) {
                client_send_batch_attach_world_baseline(&batch, server->world, proto_world.tick);
                client_queue_send_batch(client, &batch, true);
            }
            free(patch_buffer);
            client_send_batch_free(&batch);
        }

        client = client->next;
    }
    pthread_mutex_unlock(&server->clients_mutex);

    server_pump_client_outboxes(server);

    for (size_t chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
        free(chunk_buffers[chunk_idx]);
    }
    free(chunk_buffers);
    free(chunk_lengths);
    
    free(delta_parent_buffer);
    free(buffer);
    proto_world_free(&proto_world);
}

void server_send_colony_info(Server* server, ClientSession* client, uint32_t colony_id) {
    if (!server || !client || !client->socket || colony_id == 0) return;

    uint8_t* buffer = NULL;
    size_t len = 0;
    if (build_colony_info_payload(server, colony_id, &buffer, &len) < 0) {
        return;
    }

    ClientSendBatch batch = {0};
    if (client_send_batch_add_message(&batch, MSG_COLONY_INFO, buffer, len) == 0) {
        pthread_mutex_lock(&server->clients_mutex);
        if (client->active && client->socket && client->socket->connected) {
            client_queue_send_batch(client, &batch, false);
        }
        pthread_mutex_unlock(&server->clients_mutex);
    }

    client_send_batch_free(&batch);
    free(buffer);
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
                server_reset_client_transport_for_world_reset(server);
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
    protocol_recv_state_init(&session->recv_state);
    protocol_ack_window_init(&session->world_ack_window);
    
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
            
            server_free_client_session(curr);
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

    ClientSession** clients = NULL;
    size_t client_count = 0;
    if (server_collect_active_clients(server, &clients, &client_count) < 0) {
        return;
    }

    for (size_t client_idx = 0; client_idx < client_count; client_idx++) {
        ClientSession* client = clients[client_idx];
        if (!client->active || !client->socket || !client->socket->connected) {
            continue;
        }

        for (int messages = 0; messages < SERVER_CLIENT_RECV_BUDGET_MESSAGES; messages++) {
            MessageHeader header;
            uint8_t* payload = NULL;

            int result = protocol_recv_message_nonblocking(client->socket->fd,
                                                           &client->recv_state,
                                                           &header,
                                                           &payload);
            if (result == 0) {
                break;
            }
            if (result < 0) {
                printf("Client %u disconnected\n", client->id);
                server_mark_client_disconnected(client);
                break;
            }

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
                    server_mark_client_disconnected(client);
                    break;
                case MSG_ACK: {
                    ProtoAckPayload ack;
                    if (protocol_deserialize_ack(payload, header.payload_len, &ack) > 0) {
                        client_handle_world_ack(client, &ack);
                    }
                    break;
                }
                default:
                    break;
            }

            free(payload);
            if (!client->active) {
                break;
            }
        }
    }

    free(clients);
    server_pump_client_outboxes(server);
}

uint16_t server_get_port(Server* server) {
    if (!server || !server->listener) return 0;
    return server->listener->port;
}
