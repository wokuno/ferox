# Ferox Network Protocol

This document specifies the binary network protocol used for communication between Ferox server and clients.

## Overview

The Ferox protocol is a simple binary protocol over TCP designed for efficient real-time state synchronization. All multi-byte values are transmitted in **network byte order** (big-endian).

## Protocol Constants

```c
#define PROTOCOL_MAGIC    0xBACF     // "Bacteria Ferox" identifier
#define PROTOCOL_VERSION  1          // Current protocol version
#define MAX_COLONY_NAME   32         // Maximum colony name length
#define MAX_COLONIES      256        // Maximum colonies per message
#define MAX_PAYLOAD_SIZE  (1024*1024) // 1MB maximum payload
```

## Message Format

Every message consists of a fixed-size header followed by a variable-size payload.

### Message Header (14 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Magic (0xBACF)                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Type (16-bit)        |        Payload Length         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Payload Length (cont'd)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Sequence Number                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    (padding)  |
+-+-+-+-+-+-+-+-+
```

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| `magic` | 0 | 4 bytes | Protocol identifier (0x0000BACF) |
| `type` | 4 | 2 bytes | Message type enum |
| `payload_len` | 6 | 4 bytes | Payload size in bytes |
| `sequence` | 10 | 4 bytes | Message sequence number |

**Total header size: 14 bytes**

### C Structure

```c
typedef struct MessageHeader {
    uint32_t magic;      // 0xBACF
    uint16_t type;       // MessageType enum
    uint32_t payload_len;
    uint32_t sequence;   // For ordering/debugging
} MessageHeader;
```

## Message Types

```c
typedef enum MessageType {
    MSG_CONNECT     = 0,  // Client -> Server: connection request
    MSG_DISCONNECT  = 1,  // Client -> Server: graceful disconnect
    MSG_WORLD_STATE = 2,  // Server -> Client: full world state
    MSG_WORLD_DELTA = 3,  // Server -> Client: large-world grid chunk
    MSG_COLONY_INFO = 4,  // Server -> Client: detailed colony info
    MSG_COMMAND     = 5,  // Client -> Server: user command
    MSG_ACK         = 6,  // Client -> Server: world-update acknowledgment
    MSG_ERROR       = 7   // Server -> Client: error response
} MessageType;
```

## Message Payloads

### MSG_CONNECT (Type 0)

Sent by client to initiate connection. Payload is empty.

**Request:**
- Direction: Client → Server
- Payload: None

**Response:** Server sends MSG_WORLD_STATE

---

### MSG_DISCONNECT (Type 1)

Graceful disconnect notification. Payload is empty.

**Direction:** Client → Server  
**Payload:** None

Server removes client from session list.

---

### MSG_WORLD_STATE (Type 2)

Complete world state broadcast. Sent after each simulation tick.

**Direction:** Server → Client

**Payload Structure:**

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         World Width                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         World Height                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Current Tick                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Colony Count                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Paused |             Speed Multiplier (float)                 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Speed Multiplier (cont'd)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                     Colony Data Array                         |
|                     (colony_count entries)                    |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                     Grid Data (RLE Compressed)                |
|                     (width * height cells)                    |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Size | Description |
|-------|------|-------------|
| width | 4 bytes | World grid width |
| height | 4 bytes | World grid height |
| tick | 4 bytes | Current simulation tick |
| colony_count | 4 bytes | Number of colonies following |
| paused | 1 byte | 0 = running, 1 = paused |
| speed_multiplier | 4 bytes | Speed factor (IEEE 754 float) |
| colonies | variable | Array of colony structures |
| grid_data | variable | RLE-compressed grid (see Grid Serialization) |

**Colony Data Structure (per colony):**

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          Colony ID                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                    Name (32 bytes, null-padded)               |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      X Position (float)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Y Position (float)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Radius (float)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Population                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Max Population                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Growth Rate (float)                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Red  | Green | Blue  | Alive |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Border Red | Border Green | Border Blue | (padding) |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Size | Description |
|-------|------|-------------|
| id | 4 bytes | Unique colony identifier |
| name | 32 bytes | Scientific name (null-terminated) |
| x | 4 bytes | Center X position (float) |
| y | 4 bytes | Center Y position (float) |
| radius | 4 bytes | Approximate radius (float) |
| population | 4 bytes | Current cell count |
| max_population | 4 bytes | Historical peak population |
| growth_rate | 4 bytes | Growth rate (float) |
| color_r | 1 byte | Body color red component (0-255) |
| color_g | 1 byte | Body color green component (0-255) |
| color_b | 1 byte | Body color blue component (0-255) |
| alive | 1 byte | 0 = inactive/dead, 1 = active |
| border_r | 1 byte | Border color red component (0-255) |
| border_g | 1 byte | Border color green component (0-255) |
| border_b | 1 byte | Border color blue component (0-255) |
| detection_range | 4 bytes | Social detection range (float) |
| max_tracked | 1 byte | Max neighbor colonies to track |
| social_factor | 4 bytes | Attraction/repulsion factor (float) |
| merge_affinity | 4 bytes | Merge compatibility bonus (float) |
| padding | 2 bytes | Alignment padding |

**Colony serialized size: 96 bytes**

> **Note:** The `shape_seed` and `wobble_phase` fields have been removed. Colony shapes are now rendered directly from actual cell positions transmitted via the grid data, providing much more accurate territory visualization.

### C Structure (ProtoColony)

```c
typedef struct ProtoColony {
    uint32_t id;
    char name[MAX_COLONY_NAME];
    float x, y;
    float radius;
    uint32_t population;
    uint32_t max_population;         // Historical max population
    float growth_rate;
    uint8_t color_r, color_g, color_b;  // Body color
    bool alive;
    uint8_t border_r, border_g, border_b;  // Border color
    
    // Social behavior fields
    float detection_range;           // How far to detect neighbors (0.1-0.5)
    uint8_t max_tracked;             // Max neighbor colonies to track (1-4)
    float social_factor;             // -1 to +1: repulsion to attraction
    float merge_affinity;            // 0-0.3: bonus to merge compatibility
} ProtoColony;
```

### C Structure (ProtoWorld)

```c
typedef struct ProtoWorld {
    int width;
    int height;
    uint64_t tick;
    bool paused;
    float speed_multiplier;
    ProtoColony* colonies;
    size_t colony_count;
    
    // Grid data for cell-based rendering
    uint32_t* grid;                  // colony_id for each cell (width * height)
    size_t grid_size;                // Size in bytes of uncompressed grid
} ProtoWorld;
```

### Memory Management Functions

```c
// Initialize ProtoWorld with default values
void proto_world_init(ProtoWorld* world);

// Free allocated memory in ProtoWorld
void proto_world_free(ProtoWorld* world);

// Allocate grid array for given dimensions
int proto_world_alloc_grid(ProtoWorld* world, int width, int height);
```

---

### MSG_WORLD_DELTA (Type 3)

Kinding envelope for grid updates that complete a parent `MSG_WORLD_STATE`.

**Direction:** Server → Client  

The first payload byte is the delta kind:

| Kind | Name | Description |
|------|------|-------------|
| `1` | `PROTO_WORLD_DELTA_GRID_CHUNK` | contiguous raw grid slice for large-world snapshot fallback |
| `2` | `PROTO_WORLD_DELTA_GRID_PATCH` | sorted changed-cell patch against an ACKed baseline |

Grid chunk payload:

```c
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
```

Each chunk contains raw `uint16_t colony_id` values for a contiguous grid slice.
Clients assemble chunks in order and mark the grid available once the final
chunk for that tick arrives.

Changed-cell patch payload:

```c
typedef struct ProtoWorldDeltaGridPatch {
    uint32_t tick;
    uint32_t width;
    uint32_t height;
    uint32_t total_cells;
    uint32_t base_sequence;
    uint32_t change_count;
    uint32_t* indices;   // sorted ascending
    uint16_t* cells;
} ProtoWorldDeltaGridPatch;
```

Patch entries are repeated `(index:uint32, colony_id:uint16)` pairs. The server
first rejects patches that are not smaller than raw grid bytes, then sends a
patch only when the resulting parent+patch frames are smaller than the actual
full-grid fallback for that broadcast. That keeps highly compressible inline
RLE snapshots on the full-grid path. Patches are based on the latest completed
and ACKed full-grid baseline for that client. Clients accept a patch
only after the parent `MSG_WORLD_STATE` metadata arrives, the dimensions and
tick match, the current grid exists, and `base_sequence` matches the latest
ACKed world-update sequence.

---

### MSG_COLONY_INFO (Type 4)

Detailed information about a specific colony, sent when client selects a colony.

**Direction:** Server → Client

Current payload:

```c
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
```

This detail payload is sent on demand for the currently selected colony and is
also refreshed during world broadcasts while that colony remains selected.

The current selected-colony sheet exposes:

- lifecycle state (`Normal`, `Stressed`, `Dormant`)
- behavior mode and focus direction
- strongest and second-strongest current sensors and drives with strength values
- strongest live sensor -> drive link and drive -> action link
- second-ranked live sensor -> drive link and drive -> action link
- action outputs for expand, attack, defend, signal, transfer, dormancy, and motility
- stress, biofilm, signal, and drift
- age and parent id
- current action outputs for expand, attack, defend, and signal
- character summaries for expansion, aggression, resilience, cooperation,
  efficiency, and learning

---

### MSG_COMMAND (Type 5)

User command from client to server.

**Direction:** Client → Server

**Payload Structure:**

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Command Type                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                     Command Data (variable)                   |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Command Types:**

```c
typedef enum CommandType {
    CMD_PAUSE        = 0,  // Pause simulation
    CMD_RESUME       = 1,  // Resume simulation
    CMD_SPEED_UP     = 2,  // Increase simulation speed
    CMD_SLOW_DOWN    = 3,  // Decrease simulation speed
    CMD_RESET        = 4,  // Reset world to initial state
    CMD_SELECT_COLONY = 5, // Select colony for detailed view
    CMD_SPAWN_COLONY = 6   // Manually spawn colony at position
} CommandType;
```

**Command-Specific Data:**

| Command | Additional Data |
|---------|----------------|
| CMD_PAUSE | None |
| CMD_RESUME | None |
| CMD_SPEED_UP | None |
| CMD_SLOW_DOWN | None |
| CMD_RESET | None |
| CMD_SELECT_COLONY | colony_id (4 bytes) |
| CMD_SPAWN_COLONY | x (4 bytes) + y (4 bytes) + name (32 bytes) |

---

### MSG_ACK (Type 6)

Acknowledgment of completed world updates.

**Direction:** Client -> Server
**Payload:** 9 bytes

| Field | Size | Description |
|-------|------|-------------|
| channel | 1 byte | `PROTO_ACK_CHANNEL_WORLD_UPDATE` = 1 |
| latest_sequence | 4 bytes | newest fully applied parent `MSG_WORLD_STATE` header sequence |
| ack_bits | 4 bytes | bit 0 acknowledges `latest_sequence - 1`; bit 31 acknowledges `latest_sequence - 32` |

Clients send world-update ACKs only after the complete update is applied. ACKs
always name the parent `MSG_WORLD_STATE` header sequence, not follow-up chunk
message sequences. Inline grid snapshots can be ACKed immediately after
`MSG_WORLD_STATE` is applied. Chunked large-world updates are ACKed only after
the final accepted `MSG_WORLD_DELTA` grid chunk completes the local grid
assembly. Patch updates are ACKed only after the changed-cell patch applies to
the retained baseline grid. The server uses the ACK window to mark per-client
baseline-ring entries as acknowledged for future delta parents.

---

### MSG_ERROR (Type 7)

Error response from server.

**Direction:** Server → Client  
**Payload:** Error code (4 bytes) + error message (variable, null-terminated)

## Grid Serialization

The world grid is transmitted using Run-Length Encoding (RLE) compression to efficiently send cell ownership data.

### Grid Data Format

The grid contains `colony_id` values for each cell, where 0 = empty and 1+ = owned by that colony.

**Uncompressed format:** `width * height * sizeof(uint32_t)` bytes

**RLE Compressed format:** Series of (count, colony_id) pairs

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Uncompressed Size                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Compressed Size                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                   RLE Data (variable length)                  |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### RLE Encoding

Each run is encoded as:
- **Run length** (2 bytes, uint16_t): Number of consecutive cells with same colony_id (1-65535)
- **Colony ID** (4 bytes, uint32_t): The colony_id value for this run

```c
// RLE run structure
typedef struct {
    uint16_t count;     // Number of cells in this run
    uint32_t colony_id; // Colony ID (0 = empty)
} RLERun;
```

### Compression Example

For a 10x10 grid with colonies A (id=1) and B (id=2):
```
Original:  0 0 0 1 1 1 1 0 0 2 2 2 0 0 0 0 ...
Encoded:   (3, 0) (4, 1) (2, 0) (3, 2) (4, 0) ...
```

Typical compression ratios:
- Sparse grids (few colonies): 10:1 to 50:1
- Dense grids (many colonies): 2:1 to 5:1

### Serialization Functions

```c
// Serialize grid to RLE-compressed buffer
// Returns compressed size, or -1 on error
int protocol_serialize_grid_rle(const uint32_t* grid, int width, int height,
                                uint8_t** buffer, size_t* len);

// Deserialize RLE-compressed buffer to grid
// Grid must be pre-allocated to width * height
int protocol_deserialize_grid_rle(const uint8_t* buffer, size_t len,
                                  uint32_t* grid, int width, int height);
```

### Client-Side Rendering

With the grid data, clients render colonies by:
1. Drawing each cell as a colored square based on its `colony_id`
2. Using `body_color` for interior cells
3. Using `border_color` for cells adjacent to empty/enemy cells
4. Border detection: cell is border if any neighbor has different `colony_id`

```c
// Determine if cell at (x, y) is a border cell
bool is_border_cell(const uint32_t* grid, int width, int height, int x, int y) {
    uint32_t my_id = grid[y * width + x];
    if (my_id == 0) return false;  // Empty cells aren't borders
    
    // Check 4 cardinal neighbors
    int dx[] = {0, 1, 0, -1};
    int dy[] = {-1, 0, 1, 0};
    for (int i = 0; i < 4; i++) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
            return true;  // Edge of world = border
        }
        if (grid[ny * width + nx] != my_id) {
            return true;  // Different colony = border
        }
    }
    return false;
}
```

This cell-based rendering is more accurate than the previous procedural blob shapes, showing actual territory boundaries as they exist in the simulation.

---

## Serialization Details

### Integer Encoding

All integers are serialized in **network byte order** (big-endian):

```c
// Write uint32_t in network byte order
static void write_u32(uint8_t* buf, uint32_t val) {
    uint32_t net = htonl(val);
    memcpy(buf, &net, 4);
}

// Read uint32_t from network byte order
static uint32_t read_u32(const uint8_t* buf) {
    uint32_t net;
    memcpy(&net, buf, 4);
    return ntohl(net);
}
```

### Float Encoding

Floats are serialized by treating their bit representation as uint32_t:

```c
// Write float as network-order uint32_t
static void write_float(uint8_t* buf, float val) {
    uint32_t bits;
    memcpy(&bits, &val, 4);
    write_u32(buf, bits);
}

// Read float from network-order uint32_t
static float read_float(const uint8_t* buf) {
    uint32_t bits = read_u32(buf);
    float val;
    memcpy(&val, &bits, 4);
    return val;
}
```

### String Encoding

Strings are fixed-size, null-padded:

```c
// Copy string, ensuring null-termination
memcpy(buffer + offset, colony->name, MAX_COLONY_NAME);
buffer[offset + MAX_COLONY_NAME - 1] = '\0';  // Safety null
```

## Connection Handshake

```
    CLIENT                                SERVER
       │                                     │
       │          TCP SYN/ACK               │
       │◄───────────────────────────────────►│
       │                                     │
       │          MSG_CONNECT               │
       │────────────────────────────────────►│
       │                                     │
       │          MSG_WORLD_STATE           │
       │◄────────────────────────────────────│
       │                                     │
       │      (simulation running...)        │
       │                                     │
```

## State Synchronization

The server broadcasts world state at a fixed interval (default 100ms, configurable):

1. Simulation thread completes tick
2. World state is serialized to MSG_WORLD_STATE
3. World state and any grid chunks are enqueued as one ordered per-client batch
4. Server socket writes are pumped outside `clients_mutex`
5. Clients assemble complete frames from nonblocking reads
6. Clients update their local world copy and render updated state

### Handling Slow Clients

If a client cannot keep up with updates:

- The server keeps at most one active send batch and one pending send batch per
  client.
- A batch contains the world snapshot, either all associated grid chunks or one
  changed-cell patch, and optional selected-colony detail for the same broadcast
  tick.
- Once any byte from the active batch has entered the socket stream, that batch
  is never replaced. This preserves frame and chunk ordering.
- Fresh broadcasts may replace only an unsent batch, or the pending batch behind
  an already-started active batch. This coalesces stale world updates without
  corrupting partially-sent frames.
- Socket `EAGAIN` / `EWOULDBLOCK` is treated as retryable backpressure. Closed
  peers, malformed headers, and oversized payloads close the connection.
- Server/client sockets suppress `SIGPIPE` where the platform exposes a socket
  option, and Linux sends use `MSG_NOSIGNAL`, so closed peers are reported as
  send errors instead of process termination.

Chunked grid recovery remains passive and snapshot-based: clients accept chunks
only for the current world tick and expected next grid offset. Out-of-order or
mismatched chunks are ignored until a newer world snapshot restarts assembly.
There is no chunk repair, retransmit, NACK, Merkle/hash anti-entropy, or
ACK-driven resync path yet.

Large-world chunk payloads keep the same wire format whether serialized from a
temporary `uint16_t` cell array or from the protocol strided-field helper. The
server uses the strided `uint32_t` field path so chunk bytes are written
directly from `World.cells[].colony_id` without an intermediate per-chunk
staging copy.

## Command/Response Flow

```
    CLIENT                                SERVER
       │                                     │
       │     MSG_COMMAND (CMD_PAUSE)        │
       │────────────────────────────────────►│
       │                                     │ server->paused = true
       │                                     │
       │     MSG_COMMAND (SELECT_COLONY)    │
       │────────────────────────────────────►│
       │                                     │
       │          MSG_COLONY_INFO           │
       │◄────────────────────────────────────│
       │                                     │
```

## Error Handling

### Protocol Errors

| Condition | Behavior |
|-----------|----------|
| Invalid magic number | Close connection |
| Unknown message type | Send MSG_ERROR, continue |
| Payload too large | Close connection |
| Malformed payload | Send MSG_ERROR, continue |
| Payload length mismatch | Close connection |

### Network Errors

| Error | Recovery |
|-------|----------|
| Connection reset | Client should reconnect |
| Read timeout | Send keepalive or disconnect |
| Write failure | Mark client as disconnected |

### Error Codes

```c
#define ERR_UNKNOWN_COMMAND   1
#define ERR_INVALID_COLONY    2
#define ERR_INVALID_POSITION  3
#define ERR_SERVER_FULL       4
#define ERR_PROTOCOL_ERROR    5
```

## Implementation Functions

### Header Serialization

```c
int protocol_serialize_header(const MessageHeader* header, uint8_t* buffer);
int protocol_deserialize_header(const uint8_t* buffer, MessageHeader* header);
```

### World State

```c
int protocol_serialize_world_state(const ProtoWorld* world, 
                                   uint8_t** buffer, size_t* len);
int protocol_deserialize_world_state(const uint8_t* buffer, 
                                     size_t len, ProtoWorld* world);
```

### Commands

```c
int protocol_serialize_command(CommandType cmd, const void* data, 
                               uint8_t* buffer);
int protocol_deserialize_command(const uint8_t* buffer, 
                                 size_t len,
                                 CommandType* cmd, void* data);
```

Command deserialization requires the payload length so truncated command
payloads are rejected before command-specific fields are read.

### Message I/O

```c
int protocol_send_message(int socket, MessageType type, 
                          const uint8_t* payload, size_t len);
int protocol_recv_message(int socket, MessageHeader* header, 
                          uint8_t** payload);

void protocol_recv_state_init(ProtocolRecvState* state);
void protocol_recv_state_reset(ProtocolRecvState* state);
void protocol_recv_state_free(ProtocolRecvState* state);

int protocol_build_message(MessageType type, const uint8_t* payload,
                           size_t len, uint8_t** frame, size_t* frame_len);
int protocol_send_frame_nonblocking(int socket, const uint8_t* frame,
                                    size_t frame_len, size_t* offset);
int protocol_recv_message_nonblocking(int socket, ProtocolRecvState* state,
                                      MessageHeader* header, uint8_t** payload);
```

The complete-message helpers are retained for blocking/simple call sites. The
nonblocking helpers use tri-state returns: `1` complete, `0` incomplete or
would-block, and `-1` fatal error or closed peer. Built frames own a single
header+payload byte buffer and assign the sequence number once at enqueue time,
so resumed sends do not mutate wire metadata.

### Grid Compression

```c
// Compress grid data using RLE
int protocol_serialize_grid_rle(const uint32_t* grid, int width, int height,
                                uint8_t** buffer, size_t* len);

// Decompress RLE grid data
int protocol_deserialize_grid_rle(const uint8_t* buffer, size_t len,
                                  uint32_t* grid, int width, int height);
```

### ProtoWorld Management

```c
// Initialize ProtoWorld structure
void proto_world_init(ProtoWorld* world);

// Free ProtoWorld allocated memory
void proto_world_free(ProtoWorld* world);

// Allocate grid array in ProtoWorld
int proto_world_alloc_grid(ProtoWorld* world, int width, int height);
```

## Wire Examples

### MSG_CONNECT

```
Bytes (hex): 00 00 BA CF 00 00 00 00 00 00 00 00 00 01
             ├─────────┤ ├───┤ ├─────────┤ ├─────────┤
             magic      type  payload_len sequence=1
                        (0)   (0 bytes)
```

### MSG_COMMAND (CMD_PAUSE)

```
Bytes (hex): 00 00 BA CF 00 05 00 00 00 04 00 00 00 02
             ├─────────┤ ├───┤ ├─────────┤ ├─────────┤
             magic      type  payload_len sequence=2
                        (5)   (4 bytes)

Payload:     00 00 00 00
             ├─────────┤
             CMD_PAUSE=0
```

### MSG_COMMAND (CMD_SELECT_COLONY id=42)

```
Bytes (hex): 00 00 BA CF 00 05 00 00 00 08 00 00 00 03
             ├─────────┤ ├───┤ ├─────────┤ ├─────────┤
             magic      type  payload_len sequence=3
                        (5)   (8 bytes)

Payload:     00 00 00 05 00 00 00 2A
             ├─────────┤ ├─────────┤
             CMD_SELECT  colony_id=42
```
