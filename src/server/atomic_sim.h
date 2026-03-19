/**
 * atomic_sim.h - Atomic-based parallel simulation engine
 * 
 * Lock-free simulation using C11 atomics for GPU/MPI/SHMEM compatibility.
 * Uses double-buffered grid and atomic CAS for cell competition.
 */

#ifndef FEROX_ATOMIC_SIM_H
#define FEROX_ATOMIC_SIM_H

#include "../shared/cacheline.h"
#include "../shared/atomic_types.h"
#include "../shared/types.h"
#include "threadpool.h"

struct AtomicWorld;

/**
 * Work item for region-based processing.
 */
typedef struct {
    struct AtomicWorld* aworld;
    int region_index;
    int start_x, start_y;
    int end_x, end_y;
    int thread_id;
} AtomicRegionWork;

typedef struct {
    int spread_frontier_count;
    int spread_slot_capacity;
    int spread_slots_used;
    bool spread_frontier_enabled;
    uint8_t cacheline_padding[FEROX_CACHELINE_SIZE - (sizeof(int) * 3) - sizeof(bool)];
} AtomicSpreadSharedState;

_Static_assert(FEROX_CACHELINE_SIZE >= (int)((sizeof(int) * 3) + sizeof(bool)),
               "FEROX_CACHELINE_SIZE too small for AtomicSpreadSharedState");
_Static_assert(sizeof(AtomicSpreadSharedState) == FEROX_CACHELINE_SIZE,
               "AtomicSpreadSharedState should be one cacheline");

typedef struct {
    uint32_t phase_generation;
    int phase_done_count;
    int active_phase;
    int phase_region_stride;
    bool phase_shutdown;
    bool phase_system_ready;
    uint8_t cacheline_padding[FEROX_CACHELINE_SIZE - sizeof(uint32_t) - (sizeof(int) * 3) - (sizeof(bool) * 2)];
} AtomicPhaseSharedState;

_Static_assert(FEROX_CACHELINE_SIZE >= (int)(sizeof(uint32_t) + (sizeof(int) * 3) + (sizeof(bool) * 2)),
               "FEROX_CACHELINE_SIZE too small for AtomicPhaseSharedState");
_Static_assert(sizeof(AtomicPhaseSharedState) == FEROX_CACHELINE_SIZE,
               "AtomicPhaseSharedState should be one cacheline");

// ============================================================================
// Atomic World - Enhanced world with atomic operations
// ============================================================================

/**
 * AtomicWorld - World state with atomic cell grid and colony stats.
 * 
 * The atomic grid allows lock-free parallel spreading.
 * Colony stats are updated atomically during the spread phase.
 */
typedef struct AtomicWorld {
    DoubleBufferedGrid grid;         // Double-buffered atomic cells
    AtomicColonyStats* colony_stats; // Per-colony atomic counters
    size_t max_colonies;             // Capacity of colony_stats array
    
    // Original world data (for colony metadata, genomes, etc.)
    World* world;                    // Reference to original world
    
    // Work distribution
    ThreadPool* pool;
    int thread_count;
    
    // RNG seeds per thread for deterministic parallel RNG
    uint32_t* thread_seeds;

    // Precomputed region work items for parallel phases
    AtomicRegionWork* region_work;
    int region_count;

    // Reusable argument vector for batched task submission
    void** submit_args;

    // Per-region spread deltas to reduce atomic contention
    int32_t* spread_deltas;          // [region_count * max_colonies]
    uint32_t* spread_touched_ids;    // [region_count * max_colonies]
    uint32_t* spread_touched_counts; // [region_count]
    FEROX_CACHELINE_ALIGN AtomicSpreadSharedState spread_state;

    // Active-frontier tracking for sparse spread processing
    int* spread_frontier_indices;     // [width * height] active source cell indices

    // Dedicated phase workers for low-overhead atomic phases
    pthread_t* phase_threads;
    AtomicRegionWork* phase_worker_args;
    int phase_worker_count;
    int* worker_region_start;
    int* worker_region_end;
    pthread_mutex_t phase_mutex;
    pthread_cond_t phase_cond;
    pthread_cond_t phase_done_cond;
    FEROX_CACHELINE_ALIGN AtomicPhaseSharedState phase_state;

    // Run expensive serial maintenance every N ticks.
    int serial_interval;

    // Disable frontier when active source density exceeds this percentage.
    int frontier_dense_pct;
} AtomicWorld;

FEROX_CACHELINE_ASSERT_MEMBER_ALIGNED(AtomicWorld, spread_state);
FEROX_CACHELINE_ASSERT_MEMBER_ALIGNED(AtomicWorld, phase_state);

typedef struct {
    double age_ms;
    double spread_ms;
    double sync_to_world_ms;
    double serial_ms;
    double sync_from_world_ms;
    double total_ms;
} AtomicTickBreakdown;

// ============================================================================
// Initialization / Cleanup
// ============================================================================

/**
 * Create an atomic world wrapper around an existing world.
 * Allocates double-buffered grid and atomic stats.
 */
AtomicWorld* atomic_world_create(World* world, ThreadPool* pool, int thread_count);

/**
 * Destroy atomic world (does NOT destroy the underlying World).
 */
void atomic_world_destroy(AtomicWorld* aworld);

/**
 * Sync atomic grid from regular world cells.
 * Call after world initialization or external modifications.
 */
void atomic_world_sync_from_world(AtomicWorld* aworld);

/**
 * Sync regular world from atomic grid.
 * Call after parallel tick to update World for serialization.
 */
void atomic_world_sync_to_world(AtomicWorld* aworld);

// ============================================================================
// Parallel Simulation
// ============================================================================

/**
 * Run one simulation tick using atomic parallel processing.
 * 
 * Phases:
 * 1. Parallel age: Each thread ages cells in its region
 * 2. Parallel spread: Atomic CAS for cell claims, no locks
 * 3. Barrier sync
 * 4. Buffer swap
 * 5. Serial: Division/recombination detection, mutations
 * 6. Sync stats to World
 */
void atomic_tick(AtomicWorld* aworld);

/**
 * Run one simulation tick and capture coarse per-phase timing.
 */
void atomic_tick_with_breakdown(AtomicWorld* aworld, AtomicTickBreakdown* breakdown);

/**
 * Parallel spread phase only.
 * Each cell tries to spread to neighbors using atomic CAS.
 */
void atomic_spread(AtomicWorld* aworld);

/**
 * Run spread phase and apply accumulated spread deltas.
 * Useful for profiling spread-only throughput.
 */
void atomic_spread_step(AtomicWorld* aworld);

/**
 * Apply accumulated spread deltas and return total applied claims.
 * Useful for profiling spread pipeline breakdown.
 */
int64_t atomic_spread_apply_deltas(AtomicWorld* aworld);

/**
 * Enable/disable active-frontier spread mode (default: enabled).
 */
void atomic_set_spread_frontier_enabled(AtomicWorld* aworld, bool enabled);

/**
 * Return current frontier source-cell count used for spread scheduling.
 */
int atomic_get_spread_frontier_count(AtomicWorld* aworld);

/**
 * Parallel age phase only.
 * Increment age of all occupied cells atomically.
 */
void atomic_age(AtomicWorld* aworld);

/**
 * Barrier - wait for all parallel work to complete.
 */
void atomic_barrier(AtomicWorld* aworld);

// ============================================================================
// Thread-Local Work Functions (for thread pool tasks)
// ============================================================================

/**
 * Spread cells in a region using atomic operations.
 * Called by worker threads.
 */
void atomic_spread_region(AtomicRegionWork* work);

/**
 * Age cells in a region atomically.
 * Called by worker threads.
 */
void atomic_age_region(AtomicRegionWork* work);

// ============================================================================
// Statistics
// ============================================================================

/**
 * Get current population for a colony.
 */
int64_t atomic_get_population(AtomicWorld* aworld, uint32_t colony_id);

/**
 * Get max population ever for a colony.
 */
int64_t atomic_get_max_population(AtomicWorld* aworld, uint32_t colony_id);

#endif // FEROX_ATOMIC_SIM_H
