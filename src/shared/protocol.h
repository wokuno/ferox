#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PROTOCOL_MAGIC 0xBACF
#define PROTOCOL_VERSION 1
#define MAX_COLONY_NAME 32
#define MAX_COLONIES 256
#define MAX_PAYLOAD_SIZE (1024 * 1024)  // 1MB max payload
#define WOBBLE_POINTS 8  // Number of points for organic border wobble

// Message types
typedef enum MessageType {
    MSG_CONNECT,        // Client -> Server: request connection
    MSG_DISCONNECT,     // Client -> Server: disconnect
    MSG_WORLD_STATE,    // Server -> Client: full world state
    MSG_WORLD_DELTA,    // Server -> Client: changes since last update
    MSG_COLONY_INFO,    // Server -> Client: detailed colony info
    MSG_COMMAND,        // Client -> Server: user command
    MSG_ACK,            // Acknowledgment
    MSG_ERROR           // Error response
} MessageType;

// Command types
typedef enum CommandType {
    CMD_PAUSE,
    CMD_RESUME,
    CMD_SPEED_UP,
    CMD_SLOW_DOWN,
    CMD_RESET,
    CMD_SELECT_COLONY,  // Select colony for detailed view
    CMD_SPAWN_COLONY    // Manually spawn a colony at position
} CommandType;

// Message header (12 bytes)
typedef struct MessageHeader {
    uint32_t magic;      // 0xBACF (bacteria ferox)
    uint16_t type;       // MessageType
    uint32_t payload_len;
    uint32_t sequence;   // For ordering
} MessageHeader;

#define MESSAGE_HEADER_SIZE 14

// Colony data structure for serialization (prefixed to avoid conflict with types.h)
typedef struct ProtoColony {
    uint32_t id;
    char name[MAX_COLONY_NAME];
    float x, y;
    float radius;
    uint32_t population;
    uint32_t max_population;         // Historical max population
    float growth_rate;
    uint8_t color_r, color_g, color_b;
    bool alive;
    uint32_t shape_seed;             // Seed for procedural shape generation
    float wobble_phase;              // Animation phase for border movement
    float shape_evolution;           // Shape evolution factor (0-1, changes over time)
} ProtoColony;

// Colony serialized size: id(4) + name(32) + x(4) + y(4) + radius(4) + population(4) + 
//                        max_population(4) + growth_rate(4) + colors(3) + alive(1) +
//                        shape_seed(4) + wobble_phase(4) + shape_evolution(4) = 76 bytes
#define COLONY_SERIALIZED_SIZE 76

// World data structure for serialization (prefixed to avoid conflict with types.h)
typedef struct ProtoWorld {
    uint32_t width;
    uint32_t height;
    uint32_t tick;
    uint32_t colony_count;
    ProtoColony colonies[MAX_COLONIES];
    bool paused;
    float speed_multiplier;
} ProtoWorld;

// Command data structures
typedef struct CommandSelectColony {
    uint32_t colony_id;
} CommandSelectColony;

typedef struct CommandSpawnColony {
    float x, y;
    char name[MAX_COLONY_NAME];
} CommandSpawnColony;

// Serialization functions
int protocol_serialize_header(const MessageHeader* header, uint8_t* buffer);
int protocol_deserialize_header(const uint8_t* buffer, MessageHeader* header);

int protocol_serialize_world_state(const ProtoWorld* world, uint8_t** buffer, size_t* len);
int protocol_deserialize_world_state(const uint8_t* buffer, size_t len, ProtoWorld* world);

int protocol_serialize_colony(const ProtoColony* colony, uint8_t* buffer);
int protocol_deserialize_colony(const uint8_t* buffer, ProtoColony* colony);

int protocol_serialize_command(CommandType cmd, const void* data, uint8_t* buffer);
int protocol_deserialize_command(const uint8_t* buffer, CommandType* cmd, void* data);

// Helper to send/receive complete messages
int protocol_send_message(int socket, MessageType type, const uint8_t* payload, size_t len);
int protocol_recv_message(int socket, MessageHeader* header, uint8_t** payload);

#endif // PROTOCOL_H
