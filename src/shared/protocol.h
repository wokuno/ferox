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
#define MAX_INLINE_GRID_SIZE (800 * 400)  // Inline full snapshots stay under this cell count
#define MAX_GRID_CHUNK_CELLS 65536u       // 128KB of raw grid cells per delta chunk
#define WOBBLE_POINTS 8  // Number of points for organic border wobble
#define PROTO_DIRECTION_COUNT 8

typedef enum ProtoColonyBehaviorMode {
    PROTO_COLONY_BEHAVIOR_MODE_BALANCED = 0,
    PROTO_COLONY_BEHAVIOR_MODE_EXPANDING,
    PROTO_COLONY_BEHAVIOR_MODE_RAIDING,
    PROTO_COLONY_BEHAVIOR_MODE_FORTIFYING,
    PROTO_COLONY_BEHAVIOR_MODE_COOPERATING,
    PROTO_COLONY_BEHAVIOR_MODE_SURVIVAL,
    PROTO_COLONY_BEHAVIOR_MODE_DORMANT,
} ProtoColonyBehaviorMode;

typedef enum ProtoColonyBehaviorSensor {
    PROTO_COLONY_SENSOR_NUTRIENT = 0,
    PROTO_COLONY_SENSOR_TOXIN,
    PROTO_COLONY_SENSOR_SIGNAL,
    PROTO_COLONY_SENSOR_ALARM,
    PROTO_COLONY_SENSOR_FRONTIER,
    PROTO_COLONY_SENSOR_PRESSURE,
    PROTO_COLONY_SENSOR_MOMENTUM,
    PROTO_COLONY_SENSOR_GROWTH,
} ProtoColonyBehaviorSensor;

typedef enum ProtoColonyBehaviorDrive {
    PROTO_COLONY_DRIVE_GROWTH = 0,
    PROTO_COLONY_DRIVE_CAUTION,
    PROTO_COLONY_DRIVE_HOSTILITY,
    PROTO_COLONY_DRIVE_COHESION,
    PROTO_COLONY_DRIVE_EXPLORATION,
    PROTO_COLONY_DRIVE_PRESERVATION,
} ProtoColonyBehaviorDrive;

typedef enum ProtoColonyBehaviorAction {
    PROTO_COLONY_ACTION_EXPAND = 0,
    PROTO_COLONY_ACTION_ATTACK,
    PROTO_COLONY_ACTION_DEFEND,
    PROTO_COLONY_ACTION_SIGNAL,
    PROTO_COLONY_ACTION_TRANSFER,
    PROTO_COLONY_ACTION_DORMANCY,
    PROTO_COLONY_ACTION_MOTILITY,
} ProtoColonyBehaviorAction;

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

#define COLONY_DETAIL_FLAG_DORMANT 0x01u

typedef struct ProtoColonyDetail {
    ProtoColony base;
    uint32_t tick;
    uint32_t age;
    uint32_t parent_id;
    uint8_t state;
    uint8_t flags;
    uint16_t reserved;
    float stress_level;
    float biofilm_strength;
    float signal_strength;
    float drift_x;
    float drift_y;
    uint8_t behavior_mode;
    int8_t focus_direction;
    uint16_t behavior_reserved;
    uint8_t dominant_sensor;
    uint8_t dominant_drive;
    uint8_t secondary_sensor;
    uint8_t secondary_drive;
    uint8_t sensor_link_sensor;
    uint8_t sensor_link_drive;
    uint8_t action_link_drive;
    uint8_t action_link_action;
    uint8_t secondary_sensor_link_sensor;
    uint8_t secondary_sensor_link_drive;
    uint8_t secondary_action_link_drive;
    uint8_t secondary_action_link_action;
    float dominant_sensor_value;
    float dominant_drive_value;
    float secondary_sensor_value;
    float secondary_drive_value;
    float sensor_link_value;
    float action_link_value;
    float secondary_sensor_link_value;
    float secondary_action_link_value;
    float action_expand;
    float action_attack;
    float action_defend;
    float action_signal;
    float action_transfer;
    float action_dormancy;
    float action_motility;
    float trait_expansion;
    float trait_aggression;
    float trait_resilience;
    float trait_cooperation;
    float trait_efficiency;
    float trait_learning;
} ProtoColonyDetail;

#define COLONY_DETAIL_SERIALIZED_SIZE (COLONY_SERIALIZED_SIZE + 136)

// Grid cell for transmission (just colony ownership)
typedef struct ProtoCell {
    uint16_t colony_id;  // 0 = empty
} ProtoCell;

// Maximum client-side grid size supported for chunked assembly
#define MAX_GRID_SIZE (1024 * 1024)  // 1,048,576 cells max

typedef enum ProtoWorldDeltaKind {
    PROTO_WORLD_DELTA_GRID_CHUNK = 1,
} ProtoWorldDeltaKind;

typedef struct ProtoWorldDeltaGridChunk {
    uint32_t tick;
    uint32_t width;
    uint32_t height;
    uint32_t total_cells;
    uint32_t start_index;
    uint32_t cell_count;
    bool final_chunk;
    uint16_t* cells;
} ProtoWorldDeltaGridChunk;

// World data structure for serialization (prefixed to avoid conflict with types.h)
typedef struct ProtoWorld {
    uint32_t width;
    uint32_t height;
    uint32_t tick;
    uint32_t colony_count;
    ProtoColony colonies[MAX_COLONIES];
    bool paused;
    float speed_multiplier;
    
    // Grid data - actual cell ownership
    uint16_t* grid;           // Dynamically allocated [width * height]
    uint32_t grid_size;       // width * height
    bool has_grid;            // Whether grid data is included
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
int protocol_serialize_world_delta_grid_chunk(const ProtoWorldDeltaGridChunk* chunk, uint8_t** buffer, size_t* len);
int protocol_deserialize_world_delta_grid_chunk(const uint8_t* buffer, size_t len, ProtoWorldDeltaGridChunk* chunk);

int protocol_serialize_colony(const ProtoColony* colony, uint8_t* buffer);
int protocol_deserialize_colony(const uint8_t* buffer, ProtoColony* colony);
int protocol_serialize_colony_detail(const ProtoColonyDetail* detail, uint8_t* buffer);
int protocol_deserialize_colony_detail(const uint8_t* buffer, ProtoColonyDetail* detail);

int protocol_serialize_command(CommandType cmd, const void* data, uint8_t* buffer);
int protocol_deserialize_command(const uint8_t* buffer, CommandType* cmd, void* data);

// Grid serialization with RLE compression
int protocol_serialize_grid_rle(const uint16_t* grid, uint32_t size, uint8_t** buffer, size_t* len);
int protocol_deserialize_grid_rle(const uint8_t* buffer, size_t len, uint16_t* grid, uint32_t max_size);

// ProtoWorld grid memory management
void proto_world_init(ProtoWorld* world);
void proto_world_free(ProtoWorld* world);
void proto_world_alloc_grid(ProtoWorld* world, uint32_t width, uint32_t height);
void proto_world_delta_grid_chunk_init(ProtoWorldDeltaGridChunk* chunk);
void proto_world_delta_grid_chunk_free(ProtoWorldDeltaGridChunk* chunk);

// Helper to send/receive complete messages
int protocol_send_message(int socket, MessageType type, const uint8_t* payload, size_t len);
int protocol_recv_message(int socket, MessageHeader* header, uint8_t** payload);

#endif // PROTOCOL_H
