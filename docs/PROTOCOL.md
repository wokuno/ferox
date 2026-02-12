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
    MSG_WORLD_DELTA = 3,  // Server -> Client: incremental update
    MSG_COLONY_INFO = 4,  // Server -> Client: detailed colony info
    MSG_COMMAND     = 5,  // Client -> Server: user command
    MSG_ACK         = 6,  // Bidirectional: acknowledgment
    MSG_ERROR       = 7   // Server -> Client: error response
} MessageType;
```

## Message Payloads

### MSG_CONNECT (Type 0)

Sent by client to initiate connection. Payload is empty.

**Request:**
- Direction: Client → Server
- Payload: None

**Response:** Server sends MSG_ACK followed by MSG_WORLD_STATE

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
|                         Shape Seed                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Wobble Phase (float)                     |
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
| color_r | 1 byte | Red component (0-255) |
| color_g | 1 byte | Green component (0-255) |
| color_b | 1 byte | Blue component (0-255) |
| alive | 1 byte | 0 = inactive/dead, 1 = active |
| shape_seed | 4 bytes | Seed for procedural shape generation |
| wobble_phase | 4 bytes | Animation phase for border pulsing (float) |
| detection_range | 4 bytes | Social detection range (float) |
| max_tracked | 1 byte | Max neighbor colonies to track |
| social_factor | 4 bytes | Attraction/repulsion factor (float) |
| merge_affinity | 4 bytes | Merge compatibility bonus (float) |
| padding | 3 bytes | Alignment padding |

**Colony serialized size: 104 bytes**

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
    uint8_t color_r, color_g, color_b;
    bool alive;
    uint32_t shape_seed;             // Seed for procedural shape generation
    float wobble_phase;              // Animation phase for border movement
    
    // Social behavior fields
    float detection_range;           // How far to detect neighbors (0.1-0.5)
    uint8_t max_tracked;             // Max neighbor colonies to track (1-4)
    float social_factor;             // -1 to +1: repulsion to attraction
    float merge_affinity;            // 0-0.3: bonus to merge compatibility
} ProtoColony;
```

### Procedural Shape Generation

Colony shapes are generated procedurally from `shape_seed` using fractal noise, rather than storing explicit wobble points. The client computes the shape at any angle using:

```c
float colony_shape_at_angle(uint32_t shape_seed, float angle, float phase);
```

This approach is more memory efficient (4 bytes vs 32 bytes) and produces shapes with infinite angular resolution. See [GENETICS.md](GENETICS.md) for details on the procedural generation algorithm.

---

### MSG_WORLD_DELTA (Type 3)

Incremental update containing only changed cells. (Reserved for future optimization)

**Direction:** Server → Client  
**Status:** Not yet implemented

---

### MSG_COLONY_INFO (Type 4)

Detailed information about a specific colony, sent when client selects a colony.

**Direction:** Server → Client

**Payload:** Same as colony data structure in MSG_WORLD_STATE, but may include additional genome details in future versions.

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

Acknowledgment of received message.

**Direction:** Bidirectional  
**Payload:** None (or optional sequence number of acknowledged message)

---

### MSG_ERROR (Type 7)

Error response from server.

**Direction:** Server → Client  
**Payload:** Error code (4 bytes) + error message (variable, null-terminated)

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
       │          MSG_ACK                   │
       │◄────────────────────────────────────│
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
3. Message is broadcast to all connected clients
4. Clients update their local world copy
5. Clients render updated state

### Handling Slow Clients

If a client cannot keep up with updates:
- Messages queue in the TCP buffer
- Eventually, the send buffer fills
- Server may drop the client connection
- Client should handle reconnection

## Command/Response Flow

```
    CLIENT                                SERVER
       │                                     │
       │     MSG_COMMAND (CMD_PAUSE)        │
       │────────────────────────────────────►│
       │                                     │ server->paused = true
       │          MSG_ACK                   │
       │◄────────────────────────────────────│
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
                                 CommandType* cmd, void* data);
```

### Message I/O

```c
int protocol_send_message(int socket, MessageType type, 
                          const uint8_t* payload, size_t len);
int protocol_recv_message(int socket, MessageHeader* header, 
                          uint8_t** payload);
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
