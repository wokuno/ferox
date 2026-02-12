#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

// Helper to write uint32_t in network byte order
static void write_u32(uint8_t* buf, uint32_t val) {
    uint32_t net = htonl(val);
    memcpy(buf, &net, 4);
}

// Helper to read uint32_t from network byte order
static uint32_t read_u32(const uint8_t* buf) {
    uint32_t net;
    memcpy(&net, buf, 4);
    return ntohl(net);
}

// Helper to write uint16_t in network byte order
static void write_u16(uint8_t* buf, uint16_t val) {
    uint16_t net = htons(val);
    memcpy(buf, &net, 2);
}

// Helper to read uint16_t from network byte order
static uint16_t read_u16(const uint8_t* buf) {
    uint16_t net;
    memcpy(&net, buf, 2);
    return ntohs(net);
}

// Helper to write float (as uint32_t bits)
static void write_float(uint8_t* buf, float val) {
    uint32_t bits;
    memcpy(&bits, &val, 4);
    write_u32(buf, bits);
}

// Helper to read float (from uint32_t bits)
static float read_float(const uint8_t* buf) {
    uint32_t bits = read_u32(buf);
    float val;
    memcpy(&val, &bits, 4);
    return val;
}

int protocol_serialize_header(const MessageHeader* header, uint8_t* buffer) {
    if (!header || !buffer) return -1;
    
    write_u32(buffer, header->magic);
    write_u16(buffer + 4, header->type);
    write_u32(buffer + 6, header->payload_len);
    write_u32(buffer + 10, header->sequence);
    
    return MESSAGE_HEADER_SIZE;
}

int protocol_deserialize_header(const uint8_t* buffer, MessageHeader* header) {
    if (!buffer || !header) return -1;
    
    header->magic = read_u32(buffer);
    header->type = read_u16(buffer + 4);
    header->payload_len = read_u32(buffer + 6);
    header->sequence = read_u32(buffer + 10);
    
    if (header->magic != PROTOCOL_MAGIC) {
        return -1;
    }
    
    return MESSAGE_HEADER_SIZE;
}

int protocol_serialize_colony(const ProtoColony* colony, uint8_t* buffer) {
    if (!colony || !buffer) return -1;
    
    int offset = 0;
    
    write_u32(buffer + offset, colony->id);
    offset += 4;
    
    memcpy(buffer + offset, colony->name, MAX_COLONY_NAME);
    offset += MAX_COLONY_NAME;
    
    write_float(buffer + offset, colony->x);
    offset += 4;
    
    write_float(buffer + offset, colony->y);
    offset += 4;
    
    write_float(buffer + offset, colony->radius);
    offset += 4;
    
    write_u32(buffer + offset, colony->population);
    offset += 4;
    
    write_u32(buffer + offset, colony->max_population);
    offset += 4;
    
    write_float(buffer + offset, colony->growth_rate);
    offset += 4;
    
    buffer[offset++] = colony->color_r;
    buffer[offset++] = colony->color_g;
    buffer[offset++] = colony->color_b;
    buffer[offset++] = colony->alive ? 1 : 0;
    
    write_u32(buffer + offset, colony->shape_seed);
    offset += 4;
    
    write_float(buffer + offset, colony->wobble_phase);
    offset += 4;
    
    write_float(buffer + offset, colony->shape_evolution);
    offset += 4;
    
    return offset;
}

int protocol_deserialize_colony(const uint8_t* buffer, ProtoColony* colony) {
    if (!buffer || !colony) return -1;
    
    int offset = 0;
    
    colony->id = read_u32(buffer + offset);
    offset += 4;
    
    memcpy(colony->name, buffer + offset, MAX_COLONY_NAME);
    colony->name[MAX_COLONY_NAME - 1] = '\0';
    offset += MAX_COLONY_NAME;
    
    colony->x = read_float(buffer + offset);
    offset += 4;
    
    colony->y = read_float(buffer + offset);
    offset += 4;
    
    colony->radius = read_float(buffer + offset);
    offset += 4;
    
    colony->population = read_u32(buffer + offset);
    offset += 4;
    
    colony->max_population = read_u32(buffer + offset);
    offset += 4;
    
    colony->growth_rate = read_float(buffer + offset);
    offset += 4;
    
    colony->color_r = buffer[offset++];
    colony->color_g = buffer[offset++];
    colony->color_b = buffer[offset++];
    colony->alive = buffer[offset++] != 0;
    
    colony->shape_seed = read_u32(buffer + offset);
    offset += 4;
    
    colony->wobble_phase = read_float(buffer + offset);
    offset += 4;
    
    colony->shape_evolution = read_float(buffer + offset);
    offset += 4;
    
    return offset;
}

int protocol_serialize_world_state(const ProtoWorld* world, uint8_t** buffer, size_t* len) {
    if (!world || !buffer || !len) return -1;
    
    // Calculate required size
    size_t header_size = 4 + 4 + 4 + 4 + 1 + 4;  // width, height, tick, colony_count, paused, speed
    size_t colonies_size = world->colony_count * COLONY_SERIALIZED_SIZE;
    size_t total_size = header_size + colonies_size;
    
    *buffer = (uint8_t*)malloc(total_size);
    if (!*buffer) return -1;
    
    int offset = 0;
    
    write_u32(*buffer + offset, world->width);
    offset += 4;
    
    write_u32(*buffer + offset, world->height);
    offset += 4;
    
    write_u32(*buffer + offset, world->tick);
    offset += 4;
    
    write_u32(*buffer + offset, world->colony_count);
    offset += 4;
    
    (*buffer)[offset++] = world->paused ? 1 : 0;
    
    write_float(*buffer + offset, world->speed_multiplier);
    offset += 4;
    
    for (uint32_t i = 0; i < world->colony_count && i < MAX_COLONIES; i++) {
        int colony_size = protocol_serialize_colony(&world->colonies[i], *buffer + offset);
        if (colony_size < 0) {
            free(*buffer);
            *buffer = NULL;
            return -1;
        }
        offset += colony_size;
    }
    
    *len = offset;
    return 0;
}

int protocol_deserialize_world_state(const uint8_t* buffer, size_t len, ProtoWorld* world) {
    if (!buffer || !world || len < 21) return -1;
    
    int offset = 0;
    
    world->width = read_u32(buffer + offset);
    offset += 4;
    
    world->height = read_u32(buffer + offset);
    offset += 4;
    
    world->tick = read_u32(buffer + offset);
    offset += 4;
    
    world->colony_count = read_u32(buffer + offset);
    offset += 4;
    
    if (world->colony_count > MAX_COLONIES) {
        return -1;
    }
    
    world->paused = buffer[offset++] != 0;
    
    world->speed_multiplier = read_float(buffer + offset);
    offset += 4;
    
    for (uint32_t i = 0; i < world->colony_count; i++) {
        if ((size_t)offset + COLONY_SERIALIZED_SIZE > len) {
            return -1;
        }
        int colony_size = protocol_deserialize_colony(buffer + offset, &world->colonies[i]);
        if (colony_size < 0) {
            return -1;
        }
        offset += colony_size;
    }
    
    return 0;
}

int protocol_serialize_command(CommandType cmd, const void* data, uint8_t* buffer) {
    if (!buffer) return -1;
    
    int offset = 0;
    
    write_u32(buffer + offset, (uint32_t)cmd);
    offset += 4;
    
    switch (cmd) {
        case CMD_SELECT_COLONY:
            if (data) {
                const CommandSelectColony* sel = (const CommandSelectColony*)data;
                write_u32(buffer + offset, sel->colony_id);
                offset += 4;
            }
            break;
            
        case CMD_SPAWN_COLONY:
            if (data) {
                const CommandSpawnColony* spawn = (const CommandSpawnColony*)data;
                write_float(buffer + offset, spawn->x);
                offset += 4;
                write_float(buffer + offset, spawn->y);
                offset += 4;
                memcpy(buffer + offset, spawn->name, MAX_COLONY_NAME);
                offset += MAX_COLONY_NAME;
            }
            break;
            
        case CMD_PAUSE:
        case CMD_RESUME:
        case CMD_SPEED_UP:
        case CMD_SLOW_DOWN:
        case CMD_RESET:
            // No additional data needed
            break;
    }
    
    return offset;
}

int protocol_deserialize_command(const uint8_t* buffer, CommandType* cmd, void* data) {
    if (!buffer || !cmd) return -1;
    
    int offset = 0;
    
    *cmd = (CommandType)read_u32(buffer + offset);
    offset += 4;
    
    switch (*cmd) {
        case CMD_SELECT_COLONY:
            if (data) {
                CommandSelectColony* sel = (CommandSelectColony*)data;
                sel->colony_id = read_u32(buffer + offset);
                offset += 4;
            }
            break;
            
        case CMD_SPAWN_COLONY:
            if (data) {
                CommandSpawnColony* spawn = (CommandSpawnColony*)data;
                spawn->x = read_float(buffer + offset);
                offset += 4;
                spawn->y = read_float(buffer + offset);
                offset += 4;
                memcpy(spawn->name, buffer + offset, MAX_COLONY_NAME);
                spawn->name[MAX_COLONY_NAME - 1] = '\0';
                offset += MAX_COLONY_NAME;
            }
            break;
            
        case CMD_PAUSE:
        case CMD_RESUME:
        case CMD_SPEED_UP:
        case CMD_SLOW_DOWN:
        case CMD_RESET:
            // No additional data
            break;
    }
    
    return offset;
}

// Send all bytes, handling partial sends
static int send_all(int socket, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(socket, data + sent, len - sent, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        sent += n;
    }
    return 0;
}

// Receive exact number of bytes
static int recv_all(int socket, uint8_t* buffer, size_t len) {
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(socket, buffer + received, len - received, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        received += n;
    }
    return 0;
}

int protocol_send_message(int socket, MessageType type, const uint8_t* payload, size_t len) {
    static uint32_t sequence = 0;
    
    MessageHeader header = {
        .magic = PROTOCOL_MAGIC,
        .type = type,
        .payload_len = (uint32_t)len,
        .sequence = sequence++
    };
    
    uint8_t header_buf[MESSAGE_HEADER_SIZE];
    if (protocol_serialize_header(&header, header_buf) < 0) {
        return -1;
    }
    
    if (send_all(socket, header_buf, MESSAGE_HEADER_SIZE) < 0) {
        return -1;
    }
    
    if (len > 0 && payload) {
        if (send_all(socket, payload, len) < 0) {
            return -1;
        }
    }
    
    return 0;
}

int protocol_recv_message(int socket, MessageHeader* header, uint8_t** payload) {
    if (!header) return -1;
    
    uint8_t header_buf[MESSAGE_HEADER_SIZE];
    if (recv_all(socket, header_buf, MESSAGE_HEADER_SIZE) < 0) {
        return -1;
    }
    
    if (protocol_deserialize_header(header_buf, header) < 0) {
        return -1;
    }
    
    if (header->payload_len > MAX_PAYLOAD_SIZE) {
        return -1;
    }
    
    if (payload && header->payload_len > 0) {
        *payload = (uint8_t*)malloc(header->payload_len);
        if (!*payload) {
            return -1;
        }
        
        if (recv_all(socket, *payload, header->payload_len) < 0) {
            free(*payload);
            *payload = NULL;
            return -1;
        }
    } else if (payload) {
        *payload = NULL;
    }
    
    return 0;
}
