# Ferox Architecture

This document describes the technical architecture of the Ferox bacterial colony simulator.

## Current Implementation Notes

- The server currently broadcasts full `MSG_WORLD_STATE` snapshots each tick; `MSG_WORLD_DELTA` exists in protocol definitions but is not emitted by `server.c`.
- Command handling is implemented for pause/resume/speed/reset/select; `CMD_SPAWN_COLONY` is currently logged but not applied to world state.
- TUI and GUI share protocol/state parity for core controls, but GUI has extra interaction features (mouse selection, zoom, grid/info toggles) and TUI alone has `--demo`.
- `scripts/run.sh` normalizes client/server startup around port `8765` and only auto-kills existing listeners when they are `ferox_server` processes.

## System Architecture

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                            FEROX SYSTEM OVERVIEW                             │
└──────────────────────────────────────────────────────────────────────────────┘

                              ┌───────────────────┐
                              │   SERVER PROCESS  │
                              │                   │
    ┌─────────────────────────┼───────────────────┼─────────────────────────┐
    │                         │                   │                         │
    │  ┌───────────────────┐  │                   │  ┌───────────────────┐  │
    │  │   Accept Thread   │  │                   │  │ Simulation Thread │  │
    │  │                   │  │                   │  │                   │  │
    │  │ Listens for new   │  │                   │  │ Runs tick loop    │  │
    │  │ client connections│  │                   │  │ at configured     │  │
    │  │                   │  │                   │  │ tick rate         │  │
    │  └─────────┬─────────┘  │                   │  └─────────┬─────────┘  │
    │            │            │                   │            │            │
    │            ▼            │                   │            ▼            │
    │  ┌───────────────────┐  │                   │  ┌───────────────────┐  │
    │  │  Client Sessions  │  │                   │  │    Thread Pool    │  │
    │  │  (Linked List)    │  │                   │  │                   │  │
    │  │                   │  │                   │  │  ┌──┐ ┌──┐ ┌──┐   │  │
    │  │  Client 1 ──────┐ │  │                   │  │  │W1│ │W2│ │W3│   │  │
    │  │  Client 2 ────┐ │ │  │                   │  │  └──┘ └──┘ └──┘   │  │
    │  │  Client N ──┐ │ │ │  │                   │  │       ...         │  │
    │  │             │ │ │ │  │                   │  └─────────┬─────────┘  │
    │  └─────────────┼─┼─┼─┘  │                   │            │            │
    │                │ │ │    │                   │            ▼            │
    │                ▼ ▼ ▼    │                   │  ┌───────────────────┐  │
    │  ┌───────────────────┐  │                   │  │  Parallel Context │  │
    │  │   clients_mutex   │  │                   │  │                   │  │
    │  │  (pthread_mutex)  │  │                   │  │  Regions grid for │  │
    │  └───────────────────┘  │                   │  │  parallel updates │  │
    │                         │                   │  └─────────┬─────────┘  │
    │                         │                   │            │            │
    │                         │                   │            ▼            │
    │                         │  ┌─────────────┐  │  ┌───────────────────┐  │
    │                         │  │    WORLD    │◄─┼──│    Simulation     │  │
    │                         │  │             │  │  │    Functions      │  │
    │                         │  │  cells[]    │  │  │                   │  │
    │                         │  │  colonies[] │  │  │  - spread         │  │
    │                         │  │  tick       │  │  │  - mutate         │  │
    │                         │  │             │  │  │  - divide         │  │
    │                         │  └──────┬──────┘  │  │  - recombine      │  │
    │                         │         │         │  └───────────────────┘  │
    │                         └─────────┼─────────┘                         │
    │                                   │                                   │
    └───────────────────────────────────┼───────────────────────────────────┘
                                        │
                                        │ Broadcast (MSG_WORLD_STATE)
                                        │
                    ┌───────────────────┼───────────────────┐
                    │                   │                   │
                    ▼                   ▼                   ▼
           ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
           │   CLIENT 1   │    │   CLIENT 2   │    │   CLIENT N   │
           │              │    │              │    │              │
           │ ┌──────────┐ │    │ ┌──────────┐ │    │ ┌──────────┐ │
           │ │ Renderer │ │    │ │ Renderer │ │    │ │ Renderer │ │
           │ └──────────┘ │    │ └──────────┘ │    │ └──────────┘ │
           │ ┌──────────┐ │    │ ┌──────────┐ │    │ ┌──────────┐ │
           │ │  Input   │ │    │ │  Input   │ │    │ │  Input   │ │
           │ └──────────┘ │    │ └──────────┘ │    │ └──────────┘ │
           └──────────────┘    └──────────────┘    └──────────────┘
```

## Threading Model

The server uses a multi-threaded architecture with three main execution contexts:

### 1. Accept Thread
- **Purpose**: Accepts new client connections
- **Lifecycle**: Runs for the lifetime of the server
- **Synchronization**: Acquires `clients_mutex` when adding new clients

```c
// Simplified accept loop
void* accept_thread(void* arg) {
    while (server->running) {
        net_socket* socket = net_server_accept(server->listener);
        if (socket) {
            pthread_mutex_lock(&server->clients_mutex);
            server_add_client(server, socket);
            pthread_mutex_unlock(&server->clients_mutex);
        }
    }
}
```

### 2. Simulation Thread
- **Purpose**: Runs the simulation tick loop
- **Tick Rate**: Configurable (server binary default currently 50ms; `scripts/run.sh` default is 100ms)
- **Responsibilities**:
  - Advance world state
  - Broadcast updates to clients
  - Process client commands

### 3. Thread Pool (Worker Threads)
- **Purpose**: Parallel execution of simulation phases
- **Task Queue**: FIFO queue protected by mutex
- **Synchronization**: Condition variables for work notification

```c
typedef struct ThreadPool {
    pthread_t* threads;        // Worker thread handles
    int thread_count;
    Task* task_queue_head;     // FIFO task queue
    Task* task_queue_tail;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;  // Signal when work available
    pthread_cond_t done_cond;   // Signal when all work done
    int active_tasks;
    int pending_tasks;
    bool shutdown;
} ThreadPool;
```

## Atomic Simulation Engine

The simulation uses a lock-free atomic design for high-performance parallel processing, with architecture designed for future GPU/MPI/SHMEM acceleration.

### Design Principles

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                        ATOMIC SIMULATION ARCHITECTURE                         │
└──────────────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────────────┐
  │                         DOUBLE-BUFFERED GRID                             │
  │                                                                          │
  │   Buffer 0 (Read)                    Buffer 1 (Write)                    │
  │  ┌───┬───┬───┬───┬───┐             ┌───┬───┬───┬───┬───┐                │
  │  │ A │ A │ A │   │   │  ──CAS──►   │ A │ A │ A │ A │   │                │
  │  ├───┼───┼───┼───┼───┤             ├───┼───┼───┼───┼───┤                │
  │  │ A │ A │   │   │ B │  ──CAS──►   │ A │ A │ A │   │ B │                │
  │  ├───┼───┼───┼───┼───┤             ├───┼───┼───┼───┼───┤                │
  │  │   │   │   │ B │ B │  ──CAS──►   │   │   │   │ B │ B │                │
  │  └───┴───┴───┴───┴───┘             └───┴───┴───┴───┴───┘                │
  │                                                                          │
  │                     After barrier: swap buffers                          │
  └─────────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────────────┐
  │                      PARALLEL REGION PROCESSING                          │
  │                                                                          │
  │   Thread 1          Thread 2          Thread 3          Thread 4         │
  │  ┌─────────┐       ┌─────────┐       ┌─────────┐       ┌─────────┐      │
  │  │ Region  │       │ Region  │       │ Region  │       │ Region  │      │
  │  │  (0,0)  │       │  (1,0)  │       │  (0,1)  │       │  (1,1)  │      │
  │  │         │       │         │       │         │       │         │      │
  │  │ xorshift│       │ xorshift│       │ xorshift│       │ xorshift│      │
  │  │ RNG     │       │ RNG     │       │ RNG     │       │ RNG     │      │
  │  └────┬────┘       └────┬────┘       └────┬────┘       └────┬────┘      │
  │       │                 │                 │                 │           │
  │       └─────────────────┴─────────────────┴─────────────────┘           │
  │                               │                                          │
  │                         Barrier Sync                                     │
  │                               │                                          │
  │                        Buffer Swap                                       │
  └─────────────────────────────────────────────────────────────────────────┘
```

### Atomic Cell Competition

Cells compete for territory using compare-and-swap (CAS) operations instead of locks:

```c
// Atomic cell structure for lock-free updates
typedef struct {
    _Atomic uint32_t colony_id;  // 0 = empty, atomic for CAS-based spreading
    _Atomic uint8_t age;         // Atomic age counter
    uint8_t is_border;           // Border flag (read-only during parallel phase)
    uint8_t padding[2];          // Alignment padding for 32-bit access
} AtomicCell;

// Lock-free cell claim using CAS
bool atomic_cell_try_claim(AtomicCell* cell, uint32_t expected, uint32_t desired) {
    return atomic_compare_exchange_strong(&cell->colony_id, &expected, desired);
}
```

When two colonies try to spread to the same empty cell simultaneously, CAS ensures exactly one succeeds—no locks, no race conditions.

### Cascade Prevention

Newly claimed cells (age=0) skip spreading for one tick. This prevents exponential "cascade" growth where a cell claimed early in the tick could immediately spread to neighbors, causing unrealistic explosive expansion:

```c
// In atomic_spread_region():
uint8_t age = atomic_load(&cell->age);
if (age == 0) continue;  // Don't spread from newly claimed cells
```

**Growth comparison (single colony):**
| Tick | Without fix | With fix |
|------|-------------|----------|
| 1 | 1→211 | 1→4 |
| 2 | 211→2400+ | 4→14 |
| 3 | overflow | 14→31 |

### GPU/MPI/SHMEM Compatibility

The atomic design maps directly to accelerator and distributed programming models:

| C11 Atomic | CUDA | OpenCL | MPI RMA |
|------------|------|--------|---------|
| `atomic_compare_exchange_strong` | `atomicCAS` | `atomic_cmpxchg` | `MPI_Compare_and_swap` |
| `atomic_fetch_add` | `atomicAdd` | `atomic_fetch_add` | `MPI_Fetch_and_op` |
| `atomic_load` | direct read | `atomic_load` | `MPI_Get` |
| `atomic_store` | direct write | `atomic_store` | `MPI_Put` |

**Key design choices for acceleration:**
- Flat contiguous arrays map well to GPU memory (coalesced access)
- Double-buffered state avoids read-write conflicts
- Thread-local xorshift32 RNG for deterministic parallel execution
- Grid data transmitted to clients for accurate territory rendering

## Data Flow

### Server Tick Cycle

```
┌──────────────────────────────────────────────────────────────────┐
│                       SIMULATION TICK CYCLE                      │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   ┌─────────────┐                                                │
│   │ 1. Age Cells│  Increment age counter for all occupied cells  │
│   └──────┬──────┘                                                │
│          │                                                       │
│          ▼                                                       │
│   ┌──────────────┐                                               │
│   │ 2. Spread    │  Colonies expand into adjacent cells          │
│   └──────┬───────┘                                               │
│          │                                                       │
│          ▼                                                       │
│   ┌──────────────┐                                               │
│   │ 3. Mutate    │  Apply mutations to colony genomes            │
│   └──────┬───────┘                                               │
│          │                                                       │
│          ▼                                                       │
│   ┌─────────────────┐                                            │
│   │ 4. Check        │  Detect and handle colony splits           │
│   │    Divisions    │                                            │
│   └──────┬──────────┘                                            │
│          │                                                       │
│          ▼                                                       │
│   ┌─────────────────────┐                                        │
│   │ 5. Check            │  Merge compatible adjacent colonies    │
│   │    Recombinations   │                                        │
│   └──────┬──────────────┘                                        │
│          │                                                       │
│          ▼                                                       │
│   ┌──────────────────┐                                           │
│   │ 6. Increment     │  world->tick++                            │
│   │    World Tick    │                                           │
│   └──────────────────┘                                           │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### Client Message Flow

```
                    CLIENT                           SERVER
                      │                                │
                      │──── MSG_CONNECT ──────────────►│
                      │                                │
                      │◄─── MSG_ACK ──────────────────│
                      │                                │
                      │                                │ (simulation tick)
                      │◄─── MSG_WORLD_STATE ──────────│
                      │                                │
                      │◄─── MSG_WORLD_STATE ──────────│
                      │                                │
    User presses 'p'  │                                │
                      │──── MSG_COMMAND (CMD_PAUSE) ──►│
                      │                                │
                      │◄─── MSG_ACK ──────────────────│
                      │                                │
    User selects      │                                │
    colony            │──── MSG_COMMAND (SELECT) ─────►│
                      │                                │
                      │◄─── MSG_COLONY_INFO ──────────│
                      │                                │
                      │──── MSG_DISCONNECT ───────────►│
                      │                                │
```

## Module Descriptions

### Shared Modules (`src/shared/`)

| Module | Purpose |
|--------|---------|
| `types.h` | Core data structures: `Cell`, `Colony`, `Genome`, `World` |
| `atomic_types.h` | Atomic cell/stats types for lock-free parallel processing |
| `protocol.h/c` | Network message serialization/deserialization |
| `network.h/c` | Cross-platform socket abstraction |
| `colors.h/c` | HSV/RGB conversion, color blending, contrast generation |
| `names.h/c` | Procedural scientific name generation |
| `utils.h/c` | Seeded random number generator, math utilities |

### Server Modules (`src/server/`)

| Module | Purpose |
|--------|---------|
| `main.c` | Entry point, argument parsing, initialization |
| `server.h/c` | Client session management, message broadcasting |
| `world.h/c` | World creation, cell/colony access, memory management |
| `simulation.h/c` | Core simulation logic (spread, divide, recombine) |
| `genetics.h/c` | Genome operations (mutate, merge, distance) |
| `threadpool.h/c` | Thread pool with work queue |
| `parallel.h/c` | Grid partitioning for parallel updates |
| `atomic_sim.h/c` | Lock-free atomic simulation engine |

### Client Modules (`src/client/`)

| Module | Purpose |
|--------|---------|
| `main.c` | Entry point, connection setup |
| `client.h/c` | Network communication, state management |
| `renderer.h/c` | Terminal rendering with ANSI escape codes |
| `input.h/c` | Non-blocking keyboard input (raw terminal mode) |

### GUI Client (`ferox_gui/`)

The SDL2-based graphical client provides enhanced visualization using cell-based rendering.

| Feature | Description |
|---------|-------------|
| Cell-based rendering | Each cell drawn as a colored square from grid data |
| Border detection | Cells adjacent to empty/enemy use border_color |
| Zoom/pan | Mouse wheel zoom, click-drag pan |
| Colony selection | Click to select, view detailed stats |
| Continuous input | Hold keys for smooth navigation |
| Accurate territories | Shows actual cell positions from simulation |

> **Architecture Note:** The client receives RLE-compressed grid data from the server and renders cells directly. This replaces the previous procedural blob rendering system, providing more accurate territory visualization.

**Keyboard Controls (GUI):**

| Key | Action |
|-----|--------|
| Arrow keys (hold) | Pan viewport |
| `+`/`-` | Zoom in/out |
| `Space` | Pause/resume |
| `Esc` | Deselect colony |
| `Q` | Quit |

**Mouse Controls:**

| Input | Action |
|-------|--------|
| Scroll wheel | Zoom in/out |
| Left click | Select colony |
| Right/Middle drag | Pan viewport |

## Key Data Structures

### Cell
Represents a single grid position.

```c
typedef struct {
    uint32_t colony_id;  // 0 = empty
    bool is_border;      // Adjacent to different/empty colony
    uint8_t age;         // Ticks since colonized
    int8_t component_id; // For flood-fill (-1 = unmarked)
} Cell;
```

### Colony
Represents a bacterial colony species.

```c
typedef enum {
    COLONY_NORMAL,      // Normal active state
    COLONY_DORMANT,     // Low-activity survival mode
    COLONY_STRESSED     // Under environmental pressure
} ColonyState;

typedef struct {
    uint32_t id;         // Unique identifier
    char name[64];       // Scientific name (e.g., "Bacillus feroxii")
    Genome genome;       // Genetic code
    size_t cell_count;   // Number of cells owned
    uint64_t age;        // Ticks alive
    uint32_t parent_id;  // 0 if original
    bool active;         // Whether colony is alive
    Color color;         // Display color
    
    // Colony state
    ColonyState state;      // Current behavioral state
    float stress_level;     // Accumulated stress (0-1)
    float drift_x;          // Horizontal movement drift
    float drift_y;          // Vertical movement drift
    float signal_strength;  // Current chemical signal output
} Colony;
```

#### Colony States

| State | Description |
|-------|-------------|
| NORMAL | Standard growth and behavior |
| DORMANT | Reduced metabolism, high resilience, minimal spreading |
| STRESSED | Transitional state with modified behavior |

### Genome
The genetic code controlling colony behavior. See [GENETICS.md](GENETICS.md) for full trait documentation.

```c
typedef struct {
    float spread_weights[8];  // Direction weights (N,NE,E,SE,S,SW,W,NW)
    float spread_rate;        // Overall spread probability (0-1)
    float mutation_rate;      // Mutation probability (0-0.1)
    float aggression;         // Attack strength (0-1)
    float resilience;         // Defense strength (0-1)
    float metabolism;         // Growth speed modifier (0-1)
    
    // Social behavior (chemotaxis-like)
    float detection_range;    // Neighbor detection range (0.1-0.5, % of world)
    uint8_t max_tracked;      // Max colonies to track (1-4)
    float social_factor;      // Attraction/repulsion (-1 to +1)
    float merge_affinity;     // Merge bonus (0-0.3)
    
    // Environmental sensing
    float nutrient_sensitivity;  // Response to nutrient gradients (0-1)
    float edge_affinity;         // World edge preference (-1 to +1)
    float density_tolerance;     // Crowding tolerance (0-1)
    
    // Colony interactions
    float toxin_production;      // Toxin secretion rate (0-1)
    float toxin_resistance;      // Toxin immunity (0-1)
    float signal_emission;       // Signal output rate (0-1)
    float signal_sensitivity;    // Signal response (0-1)
    float gene_transfer_rate;    // Horizontal gene transfer (0-0.1)
    
    // Survival strategies
    float dormancy_threshold;    // Stress to trigger dormancy (0-1)
    float dormancy_resistance;   // Resistance to forced dormancy (0-1)
    float biofilm_investment;    // Biofilm resources (0-1)
    float motility;              // Cell movement speed (0-1)
    float motility_direction;    // Movement angle (0-2π)
    
    // Metabolic
    float efficiency;            // Metabolic efficiency (0-1)
    
    Color body_color;         // Interior cell color
    Color border_color;       // Border cell color
} Genome;
```

### World
The complete simulation state.

```c
typedef struct {
    int width;
    int height;
    Cell* cells;            // Flat array: cells[y * width + x]
    Colony* colonies;       // Dynamic array
    size_t colony_count;
    size_t colony_capacity;
    uint64_t tick;          // Current simulation tick
    
    // Environmental layers (per-cell data)
    float* nutrients;       // Nutrient level per cell (0-1)
    float* toxins;          // Toxin level per cell (0-1)
    float* signals;         // Chemical signal level per cell (0-1)
    uint32_t* signal_source; // Colony ID that emitted signal (0 = none)
} World;
```

#### Environmental Layers

| Layer | Type | Description |
|-------|------|-------------|
| `nutrients` | float[] | Nutrient concentration at each cell |
| `toxins` | float[] | Toxin concentration at each cell |
| `signals` | float[] | Chemical signal strength at each cell |
| `signal_source` | uint32_t[] | Colony that emitted the signal |

All layers use row-major indexing: `layer[y * width + x]`

## Memory Management

### Allocation Strategy

1. **World Grid**: Single contiguous allocation for all cells
   ```c
   world->cells = calloc(width * height, sizeof(Cell));
   ```

2. **Colony Array**: Dynamic array with doubling strategy
   ```c
   if (colony_count >= colony_capacity) {
       colony_capacity *= 2;
       colonies = realloc(colonies, colony_capacity * sizeof(Colony));
   }
   ```

3. **Thread Pool**: Fixed allocation at creation time

4. **Message Buffers**: Allocated per-message, freed after processing

### Ownership Rules

| Resource | Owner | Lifetime |
|----------|-------|----------|
| `World` | Server | Server lifetime |
| `Cell*` array | World | World lifetime |
| `Colony*` array | World | World lifetime |
| `client_session` | Server | Until disconnect |
| `net_socket` | Session owner | Until socket close |
| `ThreadPool` | Server | Server lifetime |
| Message payloads | Receiver | Must free after processing |

### Cleanup Order

```c
void server_destroy(Server* server) {
    // 1. Stop threads
    server->running = false;
    pthread_join(server->accept_thread, NULL);
    pthread_join(server->simulation_thread, NULL);
    
    // 2. Close client connections
    client_session* client = server->clients;
    while (client) {
        client_session* next = client->next;
        net_socket_close(client->socket);
        free(client);
        client = next;
    }
    
    // 3. Destroy thread pool
    threadpool_destroy(server->pool);
    
    // 4. Destroy parallel context
    parallel_destroy(server->parallel_ctx);
    
    // 5. Destroy world
    world_destroy(server->world);
    
    // 6. Close listener
    net_server_destroy(server->listener);
    
    // 7. Free server
    pthread_mutex_destroy(&server->clients_mutex);
    free(server);
}
```

## Concurrency Considerations

### Critical Sections

1. **Client List Access**
   - Protected by `clients_mutex`
   - Accept thread adds clients
   - Simulation thread iterates and broadcasts

2. **Task Queue**
   - Protected by `queue_mutex`
   - Condition variables signal work availability

### Parallel Simulation (Atomic)

The server uses the atomic simulation engine (`atomic_sim.h/c`) for lock-free parallel processing:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         ATOMIC TICK CYCLE                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   ┌──────────────────────────────────────────────────────────────────────┐  │
│   │                        PARALLEL PHASE                                 │  │
│   │                                                                       │  │
│   │   ┌─────────────────┐     ┌─────────────────┐                        │  │
│   │   │ 1. Parallel Age │     │ 2. Parallel     │                        │  │
│   │   │                 │────►│    Spread       │                        │  │
│   │   │ Atomic age++    │     │ Atomic CAS for  │                        │  │
│   │   │ per cell        │     │ cell claims     │                        │  │
│   │   └─────────────────┘     └────────┬────────┘                        │  │
│   │                                    │                                  │  │
│   │                              Barrier Sync                             │  │
│   │                                    │                                  │  │
│   └────────────────────────────────────┼─────────────────────────────────┘  │
│                                        │                                     │
│   ┌────────────────────────────────────┼─────────────────────────────────┐  │
│   │                        SERIAL PHASE                                   │  │
│   │                                    ▼                                  │  │
│   │   ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐        │  │
│   │   │ 3. Mutate │─►│ 4. Division│─►│5. Recomb- │─►│ 6. Update │        │  │
│   │   │           │  │   Check   │  │   ination │  │   Stats   │        │  │
│   │   └───────────┘  └───────────┘  └───────────┘  └───────────┘        │  │
│   │                                                                       │  │
│   └───────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Key differences from lock-based approach:**
- No pending change buffers—cells compete directly via CAS
- Thread-local RNG prevents contention on random state
- Atomic population counters updated in parallel
- CPU usage improved from 2-4% to 28%+ with lock-free design

### Cell-Based Rendering Architecture

The client uses cell-based rendering for accurate territory visualization:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         CLIENT RENDERING PIPELINE                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   ┌────────────────┐    ┌────────────────┐    ┌────────────────┐            │
│   │ MSG_WORLD_STATE│───►│ RLE Decompress │───►│  Grid Array    │            │
│   │   (network)    │    │   grid data    │    │ (colony_ids)   │            │
│   └────────────────┘    └────────────────┘    └───────┬────────┘            │
│                                                       │                      │
│                                                       ▼                      │
│   ┌────────────────────────────────────────────────────────────────────┐    │
│   │                       RENDER LOOP                                   │    │
│   │                                                                     │    │
│   │   For each cell (x, y):                                            │    │
│   │     1. Get colony_id from grid[y * width + x]                      │    │
│   │     2. If colony_id == 0: skip (empty)                             │    │
│   │     3. Look up colony by id for colors                             │    │
│   │     4. Check if border cell (neighbor has different colony_id)     │    │
│   │     5. Draw cell with body_color or border_color                   │    │
│   │                                                                     │    │
│   └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Benefits over procedural blob rendering:**
- Accurate territory boundaries (shows actual cell ownership)
- No approximation errors from shape functions
- Consistent rendering across all clients
- Simpler rendering code (no fractal noise computation)

### Grid Data Transmission

The grid is transmitted using RLE compression for efficiency:

| Grid Size | Uncompressed | Typical Compressed | Ratio |
|-----------|--------------|-------------------|-------|
| 100x100 | 40 KB | 2-8 KB | 5:1-20:1 |
| 500x500 | 1 MB | 50-200 KB | 5:1-20:1 |
| 1000x1000 | 4 MB | 200-800 KB | 5:1-20:1 |

Compression is effective because colonies are contiguous regions with many consecutive cells of the same colony_id.

### Distributed Computing Compatibility

The grid-based architecture is designed for future GPU and distributed computing:

| Benefit | Description |
|---------|-------------|
| **Contiguous Memory** | Flat grid array maps well to GPU global memory |
| **Row-based Decomposition** | Easy MPI domain split (rows to different nodes) |
| **Ghost Regions** | SHMEM can exchange boundary rows efficiently |
| **Deterministic** | Same simulation produces identical grids on all nodes |
| **Scalable** | Grid size independent of colony count |

> **Note:** GPU/MPI/SHMEM acceleration is planned for future work. The current implementation uses CPU threading only.

### Thread Safety Notes

- **Atomic spreading**: Uses CAS to claim cells, no pending lists needed
- Atomic population counters enable lock-free statistics updates
- Only one recombination per tick to prevent cascading merges
- Division detection marks cells before modifying colony ownership
- Thread-local xorshift32 RNG eliminates contention on random state

## Social Behavior (Chemotaxis)

Colonies exhibit chemotaxis-like social behavior, detecting and responding to nearby colonies during spreading.

### Overview

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         SOCIAL INFLUENCE SYSTEM                               │
└──────────────────────────────────────────────────────────────────────────────┘

         Colony A                            Colony B
         (social_factor = +0.8)              (social_factor = -0.6)
         ATTRACTED                           REPELLED
         
    ←─── spread ───→                    ←─── spread ───→
         │                                       │
         │    ┌─────────┐                       │
         └───►│ Colony C │◄──────────────────────┘
              │ (nearby) │       runs away
              └─────────┘
              
    Result: A spreads TOWARD C           Result: B spreads AWAY from C
            (1.5x probability in         (0.5x probability in
             direction of C)              direction of C)
```

### Genome Traits

Each colony genome includes social behavior parameters:

| Trait | Type | Range | Description |
|-------|------|-------|-------------|
| `detection_range` | float | 0.1-0.5 | How far to scan for neighbors (% of world size) |
| `max_tracked` | uint8_t | 1-4 | Maximum neighbor colonies to track |
| `social_factor` | float | -1.0 to +1.0 | -1=repelled, +1=attracted to neighbors |
| `merge_affinity` | float | 0.0-0.3 | Bonus to merge compatibility threshold |

### Detection Mechanism

1. During spreading, colonies sample nearby cells using sparse grid sampling
2. Different colonies within `detection_range` are identified
3. The nearest detected colony influences spread direction

### Influence Calculation

The `calculate_social_influence()` function computes a direction bias:

```c
// Compute direction toward/away from detected neighbor
float dx_to_neighbor = neighbor_x - cell_x;
float dy_to_neighbor = neighbor_y - cell_y;

// Compute alignment with spread direction
float alignment = (dx * dx_to_neighbor + dy * dy_to_neighbor);

// Apply social_factor: positive attracts, negative repels
float influence = 1.0f + social_factor * alignment * 0.5f;

// Result: multiplier in range [0.5, 1.5]
return clamp(influence, 0.5f, 1.5f);
```

### Effect on Spreading

The social influence multiplier (0.5x to 1.5x) is applied during `atomic_spread_region()`:

- **Positive social_factor**: Colony spreads preferentially toward detected neighbors
- **Negative social_factor**: Colony spreads preferentially away from detected neighbors
- **Zero social_factor**: No directional bias from neighbors

### Merge Affinity

The `merge_affinity` trait provides a subtle bonus to recombination compatibility:

```c
// Effective threshold = base_threshold + average_merge_affinity
float effective_threshold = 0.2f + (a->merge_affinity + b->merge_affinity) / 2.0f * 0.5f;

// Range: 0.2 (no bonus) to 0.35 (maximum bonus)
```

This makes colonies with high merge affinity more likely to merge with genetically similar neighbors.

### Evolutionary Dynamics

Social traits create interesting emergent behaviors:

| Trait Combination | Emergent Behavior |
|-------------------|-------------------|
| High social_factor + low aggression | "Follower" colonies that cluster |
| Negative social_factor + high aggression | Territorial "loner" colonies |
| High merge_affinity + high social_factor | Colony networks that readily merge |
| Low merge_affinity + high aggression | Aggressive isolationists |
