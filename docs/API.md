# Ferox API Reference

This document provides a comprehensive reference for all public functions in the Ferox codebase, organized by module.

---

## Table of Contents

- [Shared Modules](#shared-modules)
  - [types.h](#typesh)
  - [atomic_types.h](#atomic_typesh)
  - [utils.h](#utilsh)
  - [colors.h](#colorsh)
  - [names.h](#namesh)
  - [network.h](#networkh)
  - [protocol.h](#protocolh)
- [Server Modules](#server-modules)
  - [world.h](#worldh)
  - [genetics.h](#geneticsh)
  - [simulation.h](#simulationh)
  - [atomic_sim.h](#atomic_simh)
  - [threadpool.h](#threadpoolh)
  - [parallel.h](#parallelh)
  - [server.h](#serverh)
- [Client Modules](#client-modules)
  - [client.h](#clienth)
  - [renderer.h](#rendererh)
  - [input.h](#inputh)

---

## Shared Modules

### types.h

Core data structure definitions. No functions - only type definitions.

#### Types

```c
typedef enum Direction {
    DIR_N, DIR_NE, DIR_E, DIR_SE,
    DIR_S, DIR_SW, DIR_W, DIR_NW,
    DIR_COUNT  // = 8
} Direction;

typedef struct Color {
    uint8_t r, g, b;
} Color;

typedef struct Genome {
    // Basic Traits
    float spread_weights[8];  // Direction preferences (0-1)
    float spread_rate;        // Overall spread probability (0-1)
    float mutation_rate;      // Mutation probability (0-0.1)
    float aggression;         // Attack strength (0-1)
    float resilience;         // Defense strength (0-1)
    float metabolism;         // Growth speed modifier (0-1)
    
    // Social behavior (chemotaxis-like)
    float detection_range;    // Neighbor detection range (0-1)
    uint8_t max_tracked;      // Max colonies to track (1-4)
    float social_factor;      // Attraction/repulsion (-1 to +1)
    float merge_affinity;     // Merge bonus (0-1)
    
    // Environmental Sensing
    float nutrient_sensitivity;  // How strongly to follow nutrient gradients (0-1)
    float toxin_sensitivity;     // How strongly to avoid toxins (0-1)
    float edge_affinity;         // Seek/avoid world edges (-1 to +1)
    float density_tolerance;     // How well colony handles crowding (0-1)
    float quorum_threshold;      // Local density threshold for quorum sensing (0-1)
    
    // Colony Interactions
    float toxin_production;      // Toxin emission rate (0-1)
    float toxin_resistance;      // Resistance to toxin damage (0-1)
    float signal_emission;       // Chemical signal strength (0-1)
    float signal_sensitivity;    // Reaction to signals (0-1)
    float alarm_threshold;       // When to emit alarm signals (0-1)
    float gene_transfer_rate;    // Horizontal gene transfer probability (0-0.1)
    
    // Competitive Strategy
    float resource_consumption;  // Aggressive vs sustainable growth (0-1)
    float defense_priority;      // Defensive borders vs expansion (0-1)
    
    // Survival Strategies
    float dormancy_threshold;    // Population ratio triggering dormancy (0-1)
    float dormancy_resistance;   // Dormant cell resistance (0-1)
    float sporulation_threshold; // Stress level triggering dormancy (0-1)
    float biofilm_investment;    // Trade growth for resilience (0-1)
    float biofilm_tendency;      // Tendency to form biofilm (0-1)
    float motility;              // Colony drift capability (0-1)
    float motility_direction;    // Preferred drift direction (0-2π)
    float specialization;        // Edge vs interior cell differentiation (0-1)
    
    // Metabolic Strategy
    float efficiency;            // Resource conversion efficiency (0-1)
    
    // Neural Network Decision Layer
    float hidden_weights[8];     // Hidden layer weights for decisions
    float learning_rate;         // Adaptation speed (0-1)
    float memory_factor;         // Past experience influence (0-1)
    
    Color body_color;
    Color border_color;
} Genome;

typedef struct Cell { ... } Cell;
typedef struct Colony {
    uint32_t id;
    char name[64];
    Genome genome;
    size_t cell_count;
    size_t max_cell_count;   // Historical max population
    uint64_t age;
    uint32_t parent_id;
    bool active;
    Color color;
    
    // Dynamic state
    ColonyState state;        // NORMAL, DORMANT, or STRESSED
    float stress_level;       // Accumulated stress (0-1)
    float biofilm_strength;   // Current biofilm protection (0-1)
} Colony;
typedef struct World { ... } World;
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed structure documentation.

---

### atomic_types.h

Atomic data types for lock-free parallel processing, designed for GPU/MPI/SHMEM acceleration compatibility.

#### Types

```c
// Atomic cell with lock-free colony ownership
typedef struct {
    _Atomic uint32_t colony_id;  // 0 = empty, atomic for CAS-based spreading
    _Atomic uint8_t age;         // Atomic age counter
    uint8_t is_border;           // Border flag (read-only during parallel phase)
    uint8_t padding[2];          // Alignment padding for 32-bit access
} AtomicCell;

// Per-colony atomic counters for parallel updates
typedef struct {
    _Atomic int64_t cell_count;      // Current population
    _Atomic int64_t max_cell_count;  // Historical max
    _Atomic uint64_t generation;     // Mutation generation counter
} AtomicColonyStats;

// Two cell arrays for GPU-style read/write separation
typedef struct {
    AtomicCell* buffers[2];      // Two cell arrays
    int current_buffer;          // Index of current read buffer (0 or 1)
    int width;
    int height;
} DoubleBufferedGrid;
```

---

#### atomic_cell_try_claim (inline)

```c
static inline bool atomic_cell_try_claim(AtomicCell* cell, 
                                         uint32_t expected, 
                                         uint32_t desired);
```

Atomic compare-and-swap for cell ownership. Returns true if the swap succeeded.

**Parameters:**
- `cell` - Cell to claim
- `expected` - Expected current colony_id (0 for empty, or enemy id for overtake)
- `desired` - Colony ID to set if expected matches

**Returns:** true if CAS succeeded

**Portability mapping:**
- C11: `atomic_compare_exchange_strong`
- CUDA: `atomicCAS`
- OpenCL: `atomic_cmpxchg`
- MPI: `MPI_Compare_and_swap`

---

#### atomic_cell_get_colony (inline)

```c
static inline uint32_t atomic_cell_get_colony(const AtomicCell* cell);
```

Atomic load of colony_id.

**Parameters:**
- `cell` - Cell to read

**Returns:** Current colony_id (0 = empty)

---

#### atomic_cell_set_colony (inline)

```c
static inline void atomic_cell_set_colony(AtomicCell* cell, uint32_t colony_id);
```

Atomic store of colony_id.

**Parameters:**
- `cell` - Cell to update
- `colony_id` - New colony ID

---

#### atomic_cell_age (inline)

```c
static inline void atomic_cell_age(AtomicCell* cell);
```

Atomic increment of cell age (saturates at 255).

**Parameters:**
- `cell` - Cell to age

---

#### atomic_stats_add_cell (inline)

```c
static inline void atomic_stats_add_cell(AtomicColonyStats* stats);
```

Atomic increment of colony population. Also updates max if needed using lock-free max algorithm.

**Parameters:**
- `stats` - Colony stats to update

---

#### atomic_stats_remove_cell (inline)

```c
static inline void atomic_stats_remove_cell(AtomicColonyStats* stats);
```

Atomic decrement of colony population.

**Parameters:**
- `stats` - Colony stats to update

---

#### atomic_stats_get_count (inline)

```c
static inline int64_t atomic_stats_get_count(const AtomicColonyStats* stats);
```

Get current population (may be slightly stale during parallel phase).

**Parameters:**
- `stats` - Colony stats to read

**Returns:** Current cell count

---

#### grid_current (inline)

```c
static inline AtomicCell* grid_current(DoubleBufferedGrid* grid);
```

Get the current (read) buffer.

**Parameters:**
- `grid` - Double-buffered grid

**Returns:** Pointer to current buffer's cells

---

#### grid_next (inline)

```c
static inline AtomicCell* grid_next(DoubleBufferedGrid* grid);
```

Get the next (write) buffer.

**Parameters:**
- `grid` - Double-buffered grid

**Returns:** Pointer to next buffer's cells

---

#### grid_swap (inline)

```c
static inline void grid_swap(DoubleBufferedGrid* grid);
```

Swap buffers after a simulation step. Must be called after a barrier when all threads are done.

**Parameters:**
- `grid` - Double-buffered grid

---

#### grid_get_cell (inline)

```c
static inline AtomicCell* grid_get_cell(DoubleBufferedGrid* grid, int x, int y);
```

Get cell at (x, y) from current buffer.

**Parameters:**
- `grid` - Double-buffered grid
- `x` - X coordinate
- `y` - Y coordinate

**Returns:** Pointer to cell, or NULL if out of bounds

---

### utils.h

Random number generation and math utilities.

#### rng_seed

```c
void rng_seed(uint64_t seed);
```

Initialize the random number generator with a seed.

**Parameters:**
- `seed` - 64-bit seed value

**Usage:**
```c
rng_seed(12345);  // Deterministic sequence
rng_seed(time(NULL));  // Random sequence
```

---

#### rand_float

```c
float rand_float(void);
```

Generate a random float in range [0, 1).

**Returns:** Float in range [0.0, 1.0)

**Usage:**
```c
float chance = rand_float();
if (chance < 0.5f) {
    // 50% probability
}
```

---

#### rand_int

```c
int rand_int(int max);
```

Generate a random integer in range [0, max).

**Parameters:**
- `max` - Exclusive upper bound (returns 0 if max ≤ 0)

**Returns:** Integer in range [0, max)

**Usage:**
```c
int index = rand_int(array_size);  // Random array index
```

---

#### rand_range

```c
int rand_range(int min, int max);
```

Generate a random integer in range [min, max].

**Parameters:**
- `min` - Inclusive lower bound
- `max` - Inclusive upper bound

**Returns:** Integer in range [min, max]

**Usage:**
```c
int x = rand_range(0, world->width - 1);  // Random x coordinate
```

---

#### utils_clamp_f (inline)

```c
static inline float utils_clamp_f(float val, float min, float max);
```

Clamp a float value to a range.

**Parameters:**
- `val` - Value to clamp
- `min` - Minimum value
- `max` - Maximum value

**Returns:** Clamped value

---

#### utils_abs_f (inline)

```c
static inline float utils_abs_f(float val);
```

Absolute value for float.

**Parameters:**
- `val` - Input value

**Returns:** Absolute value

---

#### utils_clamp_i (inline)

```c
static inline int utils_clamp_i(int val, int min, int max);
```

Clamp an integer value to a range.

**Parameters:**
- `val` - Value to clamp
- `min` - Minimum value
- `max` - Maximum value

**Returns:** Clamped value

---

### colors.h

Color manipulation utilities.

#### hsv_to_rgb

```c
Color hsv_to_rgb(float h, float s, float v);
```

Convert HSV color to RGB.

**Parameters:**
- `h` - Hue (0-360 degrees)
- `s` - Saturation (0-1)
- `v` - Value/brightness (0-1)

**Returns:** RGB Color structure

**Usage:**
```c
Color red = hsv_to_rgb(0, 1.0f, 1.0f);    // Pure red
Color green = hsv_to_rgb(120, 1.0f, 1.0f); // Pure green
```

---

#### generate_body_color

```c
Color generate_body_color(void);
```

Generate a random vibrant color suitable for colony bodies.

**Returns:** Random RGB color with good saturation

---

#### generate_border_color

```c
Color generate_border_color(Color body_color);
```

Generate a contrasting border color for a given body color.

**Parameters:**
- `body_color` - The colony's body color

**Returns:** Darker/contrasting border color

---

#### color_distance

```c
float color_distance(Color c1, Color c2);
```

Calculate Euclidean distance between two colors in RGB space.

**Parameters:**
- `c1` - First color
- `c2` - Second color

**Returns:** Distance (0 = identical, ~441.67 = black to white)

---

#### clamp_u8

```c
uint8_t clamp_u8(int value);
```

Clamp an integer to uint8_t range [0, 255].

**Parameters:**
- `value` - Integer value

**Returns:** Clamped value as uint8_t

---

#### color_blend (inline)

```c
static inline Color color_blend(Color a, Color b, float weight_a);
```

Blend two colors with weights.

**Parameters:**
- `a` - First color
- `b` - Second color
- `weight_a` - Weight for color a (0-1, weight_b = 1 - weight_a)

**Returns:** Blended color

---

### names.h

Scientific name generation.

#### generate_scientific_name

```c
void generate_scientific_name(char* buffer, size_t buffer_size);
```

Generate a random scientific name in "Genus species" format.

**Parameters:**
- `buffer` - Output buffer (should be at least 64 bytes)
- `buffer_size` - Size of buffer

**Usage:**
```c
char name[64];
generate_scientific_name(name, sizeof(name));
// Result: "Bacillus feroxii" or similar
```

---

### network.h

Network socket abstraction.

#### net_server_create

```c
net_server* net_server_create(uint16_t port);
```

Create a TCP server listening on the specified port.

**Parameters:**
- `port` - Port number (0 for auto-assign)

**Returns:** Pointer to net_server, or NULL on failure

---

#### net_server_destroy

```c
void net_server_destroy(net_server* server);
```

Destroy server and close listening socket.

**Parameters:**
- `server` - Server to destroy

---

#### net_server_accept

```c
net_socket* net_server_accept(net_server* server);
```

Accept a new client connection (blocking).

**Parameters:**
- `server` - Listening server

**Returns:** New client socket, or NULL on failure

---

#### net_client_connect

```c
net_socket* net_client_connect(const char* host, uint16_t port);
```

Connect to a server.

**Parameters:**
- `host` - Hostname or IP address
- `port` - Port number

**Returns:** Connected socket, or NULL on failure

---

#### net_socket_close

```c
void net_socket_close(net_socket* socket);
```

Close a socket and free resources.

**Parameters:**
- `socket` - Socket to close

---

#### net_send

```c
int net_send(net_socket* socket, const uint8_t* data, size_t len);
```

Send data over socket.

**Parameters:**
- `socket` - Connected socket
- `data` - Data buffer
- `len` - Number of bytes to send

**Returns:** Bytes sent, or -1 on error

---

#### net_recv

```c
int net_recv(net_socket* socket, uint8_t* buffer, size_t max_len);
```

Receive data from socket.

**Parameters:**
- `socket` - Connected socket
- `buffer` - Receive buffer
- `max_len` - Maximum bytes to receive

**Returns:** Bytes received, 0 if would block, -1 on error/disconnect

---

#### net_has_data

```c
bool net_has_data(net_socket* socket);
```

Check if socket has data available (non-blocking).

**Parameters:**
- `socket` - Socket to check

**Returns:** true if data available

---

#### net_set_nonblocking

```c
void net_set_nonblocking(net_socket* socket, bool nonblocking);
```

Set socket blocking mode.

**Parameters:**
- `socket` - Socket to configure
- `nonblocking` - true for non-blocking, false for blocking

---

#### net_set_nodelay

```c
void net_set_nodelay(net_socket* socket, bool nodelay);
```

Enable/disable TCP_NODELAY (Nagle's algorithm).

**Parameters:**
- `socket` - Socket to configure
- `nodelay` - true to disable Nagle (low latency)

---

### protocol.h

Network protocol serialization.

#### protocol_serialize_header

```c
int protocol_serialize_header(const MessageHeader* header, uint8_t* buffer);
```

Serialize message header to buffer.

**Parameters:**
- `header` - Header to serialize
- `buffer` - Output buffer (at least MESSAGE_HEADER_SIZE bytes)

**Returns:** Bytes written, or -1 on error

---

#### protocol_deserialize_header

```c
int protocol_deserialize_header(const uint8_t* buffer, MessageHeader* header);
```

Deserialize message header from buffer.

**Parameters:**
- `buffer` - Input buffer
- `header` - Output header structure

**Returns:** Bytes read, or -1 on error (invalid magic)

---

#### protocol_serialize_world_state

```c
int protocol_serialize_world_state(const proto_world* world, 
                                   uint8_t** buffer, size_t* len);
```

Serialize complete world state.

**Parameters:**
- `world` - World data to serialize
- `buffer` - Output pointer (will be malloc'd)
- `len` - Output length

**Returns:** 0 on success, -1 on error

**Note:** Caller must free *buffer

---

#### protocol_deserialize_world_state

```c
int protocol_deserialize_world_state(const uint8_t* buffer, size_t len,
                                     proto_world* world);
```

Deserialize world state from buffer.

**Parameters:**
- `buffer` - Input buffer
- `len` - Buffer length
- `world` - Output world structure

**Returns:** 0 on success, -1 on error

---

#### protocol_send_message

```c
int protocol_send_message(int socket, MessageType type, 
                          const uint8_t* payload, size_t len);
```

Send a complete message (header + payload).

**Parameters:**
- `socket` - Socket file descriptor
- `type` - Message type
- `payload` - Payload data (may be NULL if len=0)
- `len` - Payload length

**Returns:** 0 on success, -1 on error

---

#### protocol_recv_message

```c
int protocol_recv_message(int socket, MessageHeader* header, 
                          uint8_t** payload);
```

Receive a complete message.

**Parameters:**
- `socket` - Socket file descriptor
- `header` - Output header
- `payload` - Output payload pointer (malloc'd, caller must free)

**Returns:** 0 on success, -1 on error

---

## Server Modules

### world.h

World state management.

#### world_create

```c
World* world_create(int width, int height);
```

Create a new empty world.

**Parameters:**
- `width` - Grid width
- `height` - Grid height

**Returns:** New world, or NULL on failure

---

#### world_destroy

```c
void world_destroy(World* world);
```

Destroy world and free all resources.

**Parameters:**
- `world` - World to destroy

---

#### world_init_random_colonies

```c
void world_init_random_colonies(World* world, int count);
```

Initialize world with random colonies at random positions.

**Parameters:**
- `world` - World to populate
- `count` - Number of colonies to create

---

#### world_get_cell

```c
Cell* world_get_cell(World* world, int x, int y);
```

Get cell at coordinates.

**Parameters:**
- `world` - World
- `x` - X coordinate
- `y` - Y coordinate

**Returns:** Pointer to cell, or NULL if out of bounds

---

#### world_get_colony

```c
Colony* world_get_colony(World* world, uint32_t id);
```

Get colony by ID.

**Parameters:**
- `world` - World
- `id` - Colony ID (must be > 0)

**Returns:** Pointer to colony, or NULL if not found/inactive

---

#### world_add_colony

```c
uint32_t world_add_colony(World* world, Colony colony);
```

Add a colony to the world.

**Parameters:**
- `world` - World
- `colony` - Colony to add (ID field will be assigned)

**Returns:** Assigned colony ID, or 0 on failure

---

#### world_remove_colony

```c
void world_remove_colony(World* world, uint32_t id);
```

Remove a colony (marks inactive, clears cells).

**Parameters:**
- `world` - World
- `id` - Colony ID to remove

---

### genetics.h

Genome operations.

#### genome_create_random

```c
Genome genome_create_random(void);
```

Create a random genome with valid ranges.

**Returns:** New random genome

---

#### genome_mutate

```c
void genome_mutate(Genome* genome);
```

Apply mutations based on genome's mutation_rate.

**Parameters:**
- `genome` - Genome to mutate (modified in place)

---

#### genome_distance

```c
float genome_distance(const Genome* a, const Genome* b);
```

Calculate genetic distance between two genomes.

**Parameters:**
- `a` - First genome
- `b` - Second genome

**Returns:** Distance in range [0, 1]

---

#### genome_merge

```c
Genome genome_merge(const Genome* a, size_t count_a,
                    const Genome* b, size_t count_b);
```

Merge two genomes with weighted averaging.

**Parameters:**
- `a` - First genome
- `count_a` - Cell count for genome a
- `b` - Second genome
- `count_b` - Cell count for genome b

**Returns:** Merged genome

---

#### genome_compatible

```c
bool genome_compatible(const Genome* a, const Genome* b, float threshold);
```

Check if genomes are compatible for recombination.

**Parameters:**
- `a` - First genome
- `b` - Second genome
- `threshold` - Maximum distance for compatibility (typically 0.2)

**Returns:** true if compatible

---

### simulation.h

Simulation logic.

#### simulation_tick

```c
void simulation_tick(World* world);
```

Advance simulation by one tick.

**Parameters:**
- `world` - World to update

---

#### simulation_spread

```c
void simulation_spread(World* world);
```

Process colony spreading phase.

**Parameters:**
- `world` - World to update

---

#### simulation_mutate

```c
void simulation_mutate(World* world);
```

Apply mutations to all active colonies.

**Parameters:**
- `world` - World to update

---

#### simulation_check_divisions

```c
void simulation_check_divisions(World* world);
```

Detect and handle colony divisions.

**Parameters:**
- `world` - World to check

---

#### simulation_check_recombinations

```c
void simulation_check_recombinations(World* world);
```

Detect and handle colony recombinations.

**Parameters:**
- `world` - World to check

---

#### find_connected_components

```c
int* find_connected_components(World* world, uint32_t colony_id,
                               int* num_components);
```

Find connected components of a colony using flood-fill.

**Parameters:**
- `world` - World
- `colony_id` - Colony to analyze
- `num_components` - Output: number of components found

**Returns:** Array of component sizes (caller must free)

---

### atomic_sim.h

Atomic-based parallel simulation engine using C11 atomics for GPU/MPI/SHMEM compatibility.

#### Types

```c
// Atomic world wrapper with double-buffered grid
typedef struct {
    DoubleBufferedGrid grid;         // Double-buffered atomic cells
    AtomicColonyStats* colony_stats; // Per-colony atomic counters
    size_t max_colonies;             // Capacity of colony_stats array
    World* world;                    // Reference to original world
    ThreadPool* pool;
    int thread_count;
    uint32_t* thread_seeds;          // RNG seeds per thread
} AtomicWorld;

// Work item for region-based processing
typedef struct {
    AtomicWorld* aworld;
    int start_x, start_y;
    int end_x, end_y;
    int thread_id;
} AtomicRegionWork;
```

---

#### atomic_world_create

```c
AtomicWorld* atomic_world_create(World* world, ThreadPool* pool, int thread_count);
```

Create an atomic world wrapper around an existing world. Allocates double-buffered grid and atomic stats.

**Parameters:**
- `world` - Existing world to wrap
- `pool` - Thread pool for parallel execution
- `thread_count` - Number of threads

**Returns:** New atomic world, or NULL on failure

---

#### atomic_world_destroy

```c
void atomic_world_destroy(AtomicWorld* aworld);
```

Destroy atomic world (does NOT destroy the underlying World).

**Parameters:**
- `aworld` - Atomic world to destroy

---

#### atomic_world_sync_from_world

```c
void atomic_world_sync_from_world(AtomicWorld* aworld);
```

Sync atomic grid from regular world cells. Call after world initialization or external modifications.

**Parameters:**
- `aworld` - Atomic world to sync

---

#### atomic_world_sync_to_world

```c
void atomic_world_sync_to_world(AtomicWorld* aworld);
```

Sync regular world from atomic grid. Call after parallel tick to update World for serialization.

**Parameters:**
- `aworld` - Atomic world to sync

---

#### atomic_tick

```c
void atomic_tick(AtomicWorld* aworld);
```

Run one simulation tick using atomic parallel processing.

**Phases:**
1. Parallel age: Each thread ages cells in its region
2. Parallel spread: Atomic CAS for cell claims, no locks
3. Barrier sync
4. Serial: Division/recombination detection, mutations
5. Sync stats to World

**Parameters:**
- `aworld` - Atomic world to tick

---

#### atomic_spread

```c
void atomic_spread(AtomicWorld* aworld);
```

Parallel spread phase only. Each cell tries to spread to neighbors using atomic CAS.

**Parameters:**
- `aworld` - Atomic world

---

#### atomic_age

```c
void atomic_age(AtomicWorld* aworld);
```

Parallel age phase only. Increment age of all occupied cells atomically.

**Parameters:**
- `aworld` - Atomic world

---

#### atomic_barrier

```c
void atomic_barrier(AtomicWorld* aworld);
```

Barrier - wait for all parallel work to complete.

**Parameters:**
- `aworld` - Atomic world

---

#### atomic_spread_region

```c
void atomic_spread_region(AtomicRegionWork* work);
```

Spread cells in a region using atomic operations. Called by worker threads. Applies social influence multiplier during spreading based on `calculate_social_influence()`.

**Parameters:**
- `work` - Region work item containing bounds and thread ID

---

#### calculate_social_influence (static)

```c
static float calculate_social_influence(
    World* world,
    DoubleBufferedGrid* grid,
    int cell_x, int cell_y,
    int dx, int dy,
    Colony* colony);
```

Calculate social influence multiplier for a spread direction. Detects nearby colonies and computes attraction/repulsion based on the colony's `social_factor`.

**Parameters:**
- `world` - World reference for colony data
- `grid` - Double-buffered grid for cell queries
- `cell_x`, `cell_y` - Source cell coordinates
- `dx`, `dy` - Direction of spread attempt
- `colony` - Colony attempting to spread

**Returns:** Multiplier in range [0.5, 1.5] to apply to spread probability

**Note:** This is an internal static function in `atomic_sim.c`, not part of the public API.

---

#### atomic_age_region

```c
void atomic_age_region(AtomicRegionWork* work);
```

Age cells in a region atomically. Called by worker threads.

**Parameters:**
- `work` - Region work item containing bounds

---

#### atomic_get_population

```c
int64_t atomic_get_population(AtomicWorld* aworld, uint32_t colony_id);
```

Get current population for a colony.

**Parameters:**
- `aworld` - Atomic world
- `colony_id` - Colony ID

**Returns:** Current cell count

---

#### atomic_get_max_population

```c
int64_t atomic_get_max_population(AtomicWorld* aworld, uint32_t colony_id);
```

Get max population ever for a colony.

**Parameters:**
- `aworld` - Atomic world
- `colony_id` - Colony ID

**Returns:** Maximum cell count ever reached

---

### threadpool.h

Thread pool for parallel execution.

#### threadpool_create

```c
ThreadPool* threadpool_create(int num_threads);
```

Create a thread pool.

**Parameters:**
- `num_threads` - Number of worker threads (must be > 0)

**Returns:** New thread pool, or NULL on failure

---

#### threadpool_destroy

```c
void threadpool_destroy(ThreadPool* pool);
```

Destroy thread pool (waits for pending tasks).

**Parameters:**
- `pool` - Pool to destroy

---

#### threadpool_submit

```c
void threadpool_submit(ThreadPool* pool, task_func func, void* arg);
```

Submit a task for execution.

**Parameters:**
- `pool` - Thread pool
- `func` - Task function
- `arg` - Argument to pass to function

---

#### threadpool_wait

```c
void threadpool_wait(ThreadPool* pool);
```

Wait for all pending tasks to complete.

**Parameters:**
- `pool` - Thread pool

---

### parallel.h

Parallel world update orchestration.

#### parallel_create

```c
ParallelContext* parallel_create(ThreadPool* pool, World* world,
                                 int regions_x, int regions_y);
```

Create parallel processing context.

**Parameters:**
- `pool` - Thread pool to use
- `world` - World to process (can be NULL initially)
- `regions_x` - Number of regions in X dimension
- `regions_y` - Number of regions in Y dimension

**Returns:** New context, or NULL on failure

---

#### parallel_destroy

```c
void parallel_destroy(ParallelContext* ctx);
```

Destroy parallel context (does not destroy pool or world).

**Parameters:**
- `ctx` - Context to destroy

---

#### parallel_spread

```c
void parallel_spread(ParallelContext* ctx);
```

Process spreading phase in parallel.

**Parameters:**
- `ctx` - Parallel context

---

#### parallel_mutate

```c
void parallel_mutate(ParallelContext* ctx);
```

Process mutation phase in parallel.

**Parameters:**
- `ctx` - Parallel context

---

#### parallel_barrier

```c
void parallel_barrier(ParallelContext* ctx);
```

Wait for all parallel tasks to complete.

**Parameters:**
- `ctx` - Parallel context

---

#### parallel_init_regions

```c
void parallel_init_regions(ParallelContext* ctx, 
                           int world_width, int world_height);
```

Initialize region bounds based on world dimensions.

**Parameters:**
- `ctx` - Parallel context
- `world_width` - World width
- `world_height` - World height

---

### server.h

Server core functionality.

#### server_create

```c
Server* server_create(uint16_t port, int world_width, int world_height,
                      int thread_count);
```

Create a new server.

**Parameters:**
- `port` - Listening port (0 for auto-assign)
- `world_width` - World grid width
- `world_height` - World grid height
- `thread_count` - Thread pool size

**Returns:** New server, or NULL on failure

---

#### server_destroy

```c
void server_destroy(Server* server);
```

Destroy server and all resources.

**Parameters:**
- `server` - Server to destroy

---

#### server_run

```c
void server_run(Server* server);
```

Run server main loop (blocking).

**Parameters:**
- `server` - Server to run

---

#### server_stop

```c
void server_stop(Server* server);
```

Signal server to stop.

**Parameters:**
- `server` - Server to stop

---

#### server_broadcast_world_state

```c
void server_broadcast_world_state(Server* server);
```

Send world state to all connected clients.

**Parameters:**
- `server` - Server

---

#### server_get_port

```c
uint16_t server_get_port(Server* server);
```

Get server's listening port.

**Parameters:**
- `server` - Server

**Returns:** Port number

---

## Client Modules

### client.h

Client core functionality.

#### client_create

```c
Client* client_create(void);
```

Create a new client.

**Returns:** New client, or NULL on failure

---

#### client_destroy

```c
void client_destroy(Client* client);
```

Destroy client and resources.

**Parameters:**
- `client` - Client to destroy

---

#### client_connect

```c
bool client_connect(Client* client, const char* host, uint16_t port);
```

Connect to a server.

**Parameters:**
- `client` - Client
- `host` - Server hostname
- `port` - Server port

**Returns:** true on success

---

#### client_disconnect

```c
void client_disconnect(Client* client);
```

Disconnect from server.

**Parameters:**
- `client` - Client

---

#### client_run

```c
void client_run(Client* client);
```

Run client main loop (blocking).

**Parameters:**
- `client` - Client

---

#### client_send_command

```c
void client_send_command(Client* client, CommandType cmd, void* data);
```

Send a command to the server.

**Parameters:**
- `client` - Client
- `cmd` - Command type
- `data` - Command data (may be NULL)

---

#### client_select_next_colony

```c
void client_select_next_colony(Client* client);
```

Select the next colony in the list for detailed view.

**Parameters:**
- `client` - Client

---

#### client_deselect_colony

```c
void client_deselect_colony(Client* client);
```

Deselect the currently selected colony.

**Parameters:**
- `client` - Client

---

#### client_get_selected_colony

```c
const Colony* client_get_selected_colony(Client* client);
```

Get the currently selected colony.

**Parameters:**
- `client` - Client

**Returns:** Pointer to selected colony, or NULL if none selected

---

### renderer.h

Terminal rendering.

#### renderer_create

```c
Renderer* renderer_create(void);
```

Create a new renderer.

**Returns:** New renderer, or NULL on failure

---

#### renderer_destroy

```c
void renderer_destroy(Renderer* renderer);
```

Destroy renderer.

**Parameters:**
- `renderer` - Renderer to destroy

---

#### renderer_clear

```c
void renderer_clear(Renderer* renderer);
```

Clear the frame buffer.

**Parameters:**
- `renderer` - Renderer

---

#### renderer_draw_world

```c
void renderer_draw_world(Renderer* renderer, const World* world);
```

Draw the world grid to frame buffer.

**Parameters:**
- `renderer` - Renderer
- `world` - World to draw

---

#### renderer_present

```c
void renderer_present(Renderer* renderer);
```

Output frame buffer to terminal.

**Parameters:**
- `renderer` - Renderer

---

### input.h

Keyboard input handling.

#### input_init

```c
void input_init(void);
```

Initialize input system (sets terminal to raw mode).

---

#### input_cleanup

```c
void input_cleanup(void);
```

Cleanup input system (restores terminal).

---

#### input_poll

```c
InputAction input_poll(void);
```

Poll for input action (non-blocking).

**Returns:** InputAction enum value

---

#### input_poll_char

```c
int input_poll_char(void);
```

Get raw character from input (non-blocking).

**Returns:** Character code, or -1 if no input available

---

#### input_is_initialized

```c
bool input_is_initialized(void);
```

Check if input system is initialized.

**Returns:** true if initialized
