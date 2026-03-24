# Ferox Network Protocol

This document describes the current live Ferox wire format as implemented in
`src/shared/protocol.h` and `src/shared/protocol.c`.

## Overview

- Transport: TCP
- Byte order: network byte order (big-endian) for integers and float bit-patterns
- Current documented wire generation: `PROTOCOL_VERSION == 1`
- Compatibility status: the version constant is not serialized on the wire yet;
  peers are expected to speak the same protocol generation

## Protocol Constants

```c
#define PROTOCOL_MAGIC 0xBACF
#define PROTOCOL_VERSION 1
#define MAX_COLONY_NAME 32
#define MAX_COLONIES 256
#define MAX_PAYLOAD_SIZE (1024 * 1024)
#define MAX_INLINE_GRID_SIZE (800 * 400)
#define MAX_GRID_CHUNK_CELLS 65536u
```

## Versioning Status

The current transport has a documented wire generation (`PROTOCOL_VERSION`) but
does not serialize that value in the message header or connect handshake. In
practice, that means:

- current clients and servers must be upgraded together
- the protocol spec must track the live implementation exactly
- any future incompatible wire change should add explicit negotiation or a
  serialized version field before rollout rather than silently reinterpreting
  existing fields

This PR keeps the live wire format intact and makes the current expectations
explicit instead of introducing a broad protocol redesign.

## Message Header

Every message starts with a 14-byte header.

| Field | Offset | Size | Type |
|-------|--------|------|------|
| `magic` | 0 | 4 | `uint32_t` |
| `type` | 4 | 2 | `uint16_t` |
| `payload_len` | 6 | 4 | `uint32_t` |
| `sequence` | 10 | 4 | `uint32_t` |

```c
typedef struct MessageHeader {
    uint32_t magic;
    uint16_t type;
    uint32_t payload_len;
    uint32_t sequence;
} MessageHeader;

#define MESSAGE_HEADER_SIZE 14
```

Example header bytes for `MSG_COMMAND` with an 8-byte payload and sequence 3:

```text
00 00 BA CF 00 05 00 00 00 08 00 00 00 03
```

## Message Types

```c
typedef enum MessageType {
    MSG_CONNECT,
    MSG_DISCONNECT,
    MSG_WORLD_STATE,
    MSG_WORLD_DELTA,
    MSG_COLONY_INFO,
    MSG_COMMAND,
    MSG_ACK,
    MSG_ERROR
} MessageType;
```

## Payloads

### MSG_CONNECT

- Direction: client -> server
- Payload: none
- Current behavior: clients send this immediately after TCP connect; the server
  accepts the connection and then begins normal world-state broadcasting

### MSG_DISCONNECT

- Direction: client -> server
- Payload: none

### MSG_WORLD_STATE

`MSG_WORLD_STATE` carries the fixed world metadata, colony list, and optionally
an inline compressed grid.

Fixed prefix layout (26 bytes):

| Field | Size | Type |
|-------|------|------|
| `width` | 4 | `uint32_t` |
| `height` | 4 | `uint32_t` |
| `tick` | 4 | `uint32_t` |
| `colony_count` | 4 | `uint32_t` |
| `paused` | 1 | `bool` as byte |
| `speed_multiplier` | 4 | `float` |
| `has_grid` | 1 | `bool` as byte |
| `grid_len` | 4 | `uint32_t` |

After the fixed prefix:

1. `colony_count` serialized `ProtoColony` entries
2. `grid_len` bytes of compressed grid data when `has_grid == 1`

```c
typedef struct ProtoWorld {
    uint32_t width;
    uint32_t height;
    uint32_t tick;
    uint32_t colony_count;
    ProtoColony colonies[MAX_COLONIES];
    bool paused;
    float speed_multiplier;
    uint16_t* grid;
    uint32_t grid_size;
    bool has_grid;
} ProtoWorld;
```

Notes:

- `tick` is currently 32-bit on the wire
- `grid` cells are `uint16_t colony_id` values
- small/medium worlds may inline the grid in `MSG_WORLD_STATE`
- larger worlds send colony metadata in `MSG_WORLD_STATE` and stream the grid in
  ordered `MSG_WORLD_DELTA` chunks

Example fixed prefix for a paused `7x9` world at tick `123`, speed `1.5`, no inline grid:

```text
00 00 00 07 00 00 00 09 00 00 00 7B 00 00 00 00 01 3F C0 00 00 00 00 00 00 00
```

### ProtoColony

The live wire format for each colony is 76 bytes.

| Field | Size | Type |
|-------|------|------|
| `id` | 4 | `uint32_t` |
| `name` | 32 | fixed byte array |
| `x` | 4 | `float` |
| `y` | 4 | `float` |
| `radius` | 4 | `float` |
| `population` | 4 | `uint32_t` |
| `max_population` | 4 | `uint32_t` |
| `growth_rate` | 4 | `float` |
| `color_r` | 1 | `uint8_t` |
| `color_g` | 1 | `uint8_t` |
| `color_b` | 1 | `uint8_t` |
| `alive` | 1 | `bool` as byte |
| `shape_seed` | 4 | `uint32_t` |
| `wobble_phase` | 4 | `float` |
| `shape_evolution` | 4 | `float` |

```c
typedef struct ProtoColony {
    uint32_t id;
    char name[MAX_COLONY_NAME];
    float x, y;
    float radius;
    uint32_t population;
    uint32_t max_population;
    float growth_rate;
    uint8_t color_r, color_g, color_b;
    bool alive;
    uint32_t shape_seed;
    float wobble_phase;
    float shape_evolution;
} ProtoColony;

#define COLONY_SERIALIZED_SIZE 76
```

Important current behavior:

- `name` is copied as 32 raw bytes on the wire
- deserialization forces `name[31] = '\0'` locally for safety
- visual fields (`shape_seed`, `wobble_phase`, `shape_evolution`) are still part
  of the live wire format today

### MSG_WORLD_DELTA

`MSG_WORLD_DELTA` is currently used for chunked grid transport only.

Payload layout:

| Field | Size | Type |
|-------|------|------|
| `kind` | 1 | `uint8_t` |
| `tick` | 4 | `uint32_t` |
| `width` | 4 | `uint32_t` |
| `height` | 4 | `uint32_t` |
| `total_cells` | 4 | `uint32_t` |
| `start_index` | 4 | `uint32_t` |
| `cell_count` | 4 | `uint32_t` |
| `final_chunk` | 1 | `bool` as byte |
| `cells` | `2 * cell_count` | `uint16_t[]` |

```c
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
```

Current server behavior:

- when the world grid is too large to inline, the server first sends
  `MSG_WORLD_STATE` with `has_grid = 0` and `grid_len = 0`
- it then sends ordered `MSG_WORLD_DELTA` chunks for the same tick
- clients assemble chunks sequentially and mark the grid available after the
  final chunk arrives

Example chunk payload for three cells `{1, 256, 513}`:

```text
01
01 02 03 04
00 00 00 20
00 00 00 10
00 00 02 00
00 00 00 0A
00 00 00 03
01
00 01 01 00 02 01
```

### MSG_COLONY_INFO

`MSG_COLONY_INFO` sends the serialized `ProtoColonyDetail` payload for the
currently selected colony.

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

#define COLONY_DETAIL_SERIALIZED_SIZE (COLONY_SERIALIZED_SIZE + 136)
```

`COLONY_DETAIL_SERIALIZED_SIZE` is currently 212 bytes.

### MSG_COMMAND

Commands are serialized as a 4-byte `CommandType` followed by command-specific
data.

```c
typedef enum CommandType {
    CMD_PAUSE,
    CMD_RESUME,
    CMD_SPEED_UP,
    CMD_SLOW_DOWN,
    CMD_RESET,
    CMD_SELECT_COLONY,
    CMD_SPAWN_COLONY
} CommandType;
```

| Command | Extra payload |
|---------|---------------|
| `CMD_PAUSE` | none |
| `CMD_RESUME` | none |
| `CMD_SPEED_UP` | none |
| `CMD_SLOW_DOWN` | none |
| `CMD_RESET` | none |
| `CMD_SELECT_COLONY` | `uint32_t colony_id` |
| `CMD_SPAWN_COLONY` | `float x`, `float y`, `char name[32]` |

Current server behavior for `CMD_SPAWN_COLONY`:

- rounds the requested coordinates to integer grid cells
- creates a one-cell colony only when the target cell is in bounds and empty
- uses the provided fixed-width name when non-empty, otherwise generates a
  scientific fallback name
- rejects occupied or out-of-bounds targets without mutating the world
- now follows the command with structured `MSG_ACK` or `MSG_ERROR` feedback so
  clients can distinguish accepted versus rejected requests immediately

Examples:

```text
CMD_PAUSE payload:
00 00 00 00

CMD_SELECT_COLONY payload for colony 42:
00 00 00 05 00 00 00 2A
```

### MSG_ACK

`MSG_ACK` now carries `ProtoCommandStatus` for accepted command-side outcomes.

Current live use:

- accepted `CMD_SPAWN_COLONY` requests return `MSG_ACK`
- accepted `CMD_SELECT_COLONY` requests also return `MSG_ACK`
- the payload records the originating command id, a status code, the spawned
  or selected colony id, and a short fixed-width message
- clearing selection with `CMD_SELECT_COLONY` and `colony_id = 0` returns
  `PROTO_COMMAND_STATUS_ACCEPTED` with `entity_id = 0`

### MSG_ERROR

`MSG_ERROR` now carries the same `ProtoCommandStatus` payload shape for rejected
command-side outcomes.

Current live use:

- rejected `CMD_SPAWN_COLONY` requests return `MSG_ERROR`
- rejected `CMD_SELECT_COLONY` requests also return `MSG_ERROR`
- out-of-bounds requests use `PROTO_COMMAND_STATUS_OUT_OF_BOUNDS`
- occupied-target requests use `PROTO_COMMAND_STATUS_CONFLICT`
- unknown or inactive selection targets use `PROTO_COMMAND_STATUS_REJECTED`

`ProtoCommandStatus` wire layout:

| Field | Size | Type |
|-------|------|------|
| `command` | 4 | `uint32_t` |
| `status_code` | 4 | `uint32_t` |
| `entity_id` | 4 | `uint32_t` |
| `message` | 64 | fixed byte array |

```c
typedef struct ProtoCommandStatus {
    uint32_t command;
    uint32_t status_code;
    uint32_t entity_id;
    char message[64];
} ProtoCommandStatus;
```

## Grid Codec

The grid codec operates on `uint16_t` colony ids.

Wire layout:

| Field | Size | Type |
|-------|------|------|
| `uncompressed_size` | 4 | `uint32_t` |
| `mode` | 1 | `uint8_t` |
| `payload` | variable | mode-specific |

Modes:

- `0`: RLE payload as repeated `[count:uint16_t][value:uint16_t]`
- `1`: raw payload as repeated `[value:uint16_t]`

Current behavior:

- the serializer samples the grid and can choose raw mode early for noisy input
- it also falls back to raw mode when full RLE output is not smaller
- deserialization rejects unknown mode values

Function signatures:

```c
int protocol_serialize_grid_rle(const uint16_t* grid, uint32_t size,
                                uint8_t** buffer, size_t* len);
int protocol_deserialize_grid_rle(const uint8_t* buffer, size_t len,
                                  uint16_t* grid, uint32_t max_size);
```

## Serialization Rules

- integers use big-endian encoding
- floats are serialized by reinterpreting the 32-bit IEEE-754 bit pattern and
  then writing that value as big-endian `uint32_t`
- booleans are serialized as one byte (`0` or `1`)
- names are fixed-width byte arrays and may contain any byte values; receivers
  should not assume UTF-8

## Runtime Message Flow

Current high-level flow:

1. client opens a TCP connection
2. client sends `MSG_CONNECT`
3. server adds the client session
4. each simulation tick, server sends `MSG_WORLD_STATE`
5. for large worlds, server follows with ordered `MSG_WORLD_DELTA` grid chunks
6. if a colony is selected, server may also send `MSG_COLONY_INFO`

## Conformance Coverage

Protocol-focused conformance checks live in:

- `tests/test_protocol_edge.c`
- `tests/test_simulation_logic.c`
- `tests/test_perf_unit_protocol.c`

These tests now cover documented wire examples, fixed-prefix world-state bytes,
delta chunk bytes, raw-grid mode round trips, and error handling for malformed
grid codec mode values.
