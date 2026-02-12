/**
 * atomic_sim.h - Atomic-based parallel simulation engine
 * 
 * Lock-free simulation using C11 atomics for GPU/MPI/SHMEM compatibility.
 * Uses double-buffered grid and atomic CAS for cell competition.
 */

#ifndef FEROX_ATOMIC_SIM_H
#define FEROX_ATOMIC_SIM_H

#include "../shared/atomic_types.h"
#include "../shared/types.h"
#include "threadpool.h"

// ============================================================================
// Atomic World - Enhanced world with atomic operations
// ============================================================================

/**
 * AtomicWorld - World state with atomic cell grid and colony stats.
 * 
 * The atomic grid allows lock-free parallel spreading.
 * Colony stats are updated atomically during the spread phase.
 */
typedef struct {
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
} AtomicWorld;

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
 * Parallel spread phase only.
 * Each cell tries to spread to neighbors using atomic CAS.
 */
void atomic_spread(AtomicWorld* aworld);

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
 * Work item for region-based processing.
 */
typedef struct {
    AtomicWorld* aworld;
    int start_x, start_y;
    int end_x, end_y;
    int thread_id;
} AtomicRegionWork;

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
