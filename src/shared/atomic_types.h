/**
 * atomic_types.h - Atomic data types for lock-free parallel processing
 * 
 * Designed for future GPU/MPI/SHMEM acceleration compatibility:
 * - C11 atomics map to CUDA atomics, OpenCL atomics, MPI RMA atomics
 * - Flat contiguous arrays map well to GPU memory
 * - Double-buffered state avoids read-write conflicts
 * - Atomic CAS enables lock-free cell competition
 */

#ifndef FEROX_ATOMIC_TYPES_H
#define FEROX_ATOMIC_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

// ============================================================================
// Atomic Cell - Lock-free cell with atomic colony ownership
// ============================================================================

/**
 * AtomicCell - A cell with atomic colony_id for lock-free updates.
 * 
 * The colony_id field uses atomic operations to allow concurrent
 * spreading without locks. Age is updated atomically as well.
 * 
 * Memory layout is GPU-friendly (32-bit aligned).
 */
typedef struct {
    _Atomic uint32_t colony_id;  // 0 = empty, atomic for CAS-based spreading
    _Atomic uint8_t age;         // Atomic age counter
    uint8_t is_border;           // Border flag (read-only during parallel phase)
    uint8_t padding[2];          // Alignment padding for 32-bit access
} AtomicCell;

// ============================================================================
// Atomic Colony Stats - Lock-free population tracking
// ============================================================================

/**
 * AtomicColonyStats - Per-colony atomic counters for parallel updates.
 * 
 * Separated from Colony struct for cache-line optimization.
 * Each colony has its own stats block to avoid false sharing.
 */
typedef struct {
    _Atomic int64_t cell_count;      // Current population (can go negative temporarily during CAS)
    _Atomic int64_t max_cell_count;  // Historical max (updated with atomic max)
    _Atomic uint64_t generation;     // Mutation generation counter
} AtomicColonyStats;

// ============================================================================
// Double-Buffered Grid - For GPU-style read-write separation
// ============================================================================

/**
 * DoubleBufferedGrid - Two cell arrays for parallel read/write.
 * 
 * During simulation:
 * 1. Read from 'current' buffer
 * 2. Write to 'next' buffer using atomic CAS
 * 3. Swap buffers after barrier
 * 
 * This pattern maps directly to GPU ping-pong buffers.
 */
typedef struct {
    AtomicCell* buffers[2];      // Two cell arrays
    int current_buffer;          // Index of current read buffer (0 or 1)
    int width;
    int height;
} DoubleBufferedGrid;

// ============================================================================
// Atomic Operations - Portable wrappers for GPU/MPI compatibility
// ============================================================================

/**
 * Atomic compare-and-swap for cell ownership.
 * Returns true if the swap succeeded.
 * 
 * Maps to:
 * - C11: atomic_compare_exchange_strong
 * - CUDA: atomicCAS
 * - OpenCL: atomic_cmpxchg
 * - MPI: MPI_Compare_and_swap
 */
static inline bool atomic_cell_try_claim(AtomicCell* cell, uint32_t expected, uint32_t desired) {
    return atomic_compare_exchange_strong(&cell->colony_id, &expected, desired);
}

/**
 * Atomic load of colony_id.
 */
static inline uint32_t atomic_cell_get_colony(const AtomicCell* cell) {
    return atomic_load(&cell->colony_id);
}

/**
 * Atomic store of colony_id.
 */
static inline void atomic_cell_set_colony(AtomicCell* cell, uint32_t colony_id) {
    atomic_store(&cell->colony_id, colony_id);
}

/**
 * Atomic increment of cell age (saturates at 255).
 */
static inline void atomic_cell_age(AtomicCell* cell) {
    uint8_t old_age = atomic_load(&cell->age);
    if (old_age < 255) {
        atomic_fetch_add(&cell->age, 1);
    }
}

/**
 * Atomic increment of colony population.
 */
static inline void atomic_stats_add_cell(AtomicColonyStats* stats) {
    int64_t new_count = atomic_fetch_add(&stats->cell_count, 1) + 1;
    // Update max if needed (lock-free max)
    int64_t old_max = atomic_load(&stats->max_cell_count);
    while (new_count > old_max) {
        if (atomic_compare_exchange_weak(&stats->max_cell_count, &old_max, new_count)) {
            break;
        }
    }
}

/**
 * Atomic decrement of colony population.
 */
static inline void atomic_stats_remove_cell(AtomicColonyStats* stats) {
    atomic_fetch_sub(&stats->cell_count, 1);
}

/**
 * Get current population (may be slightly stale in parallel phase).
 */
static inline int64_t atomic_stats_get_count(const AtomicColonyStats* stats) {
    return atomic_load(&stats->cell_count);
}

/**
 * Get max population ever reached.
 */
static inline int64_t atomic_stats_get_max(const AtomicColonyStats* stats) {
    return atomic_load(&stats->max_cell_count);
}

// ============================================================================
// Double-Buffer Operations
// ============================================================================

/**
 * Get the current (read) buffer.
 */
static inline AtomicCell* grid_current(DoubleBufferedGrid* grid) {
    return grid->buffers[grid->current_buffer];
}

/**
 * Get the next (write) buffer.
 */
static inline AtomicCell* grid_next(DoubleBufferedGrid* grid) {
    return grid->buffers[1 - grid->current_buffer];
}

/**
 * Swap buffers after a simulation step.
 * Must be called after a barrier when all threads are done.
 */
static inline void grid_swap(DoubleBufferedGrid* grid) {
    grid->current_buffer = 1 - grid->current_buffer;
}

/**
 * Get cell at (x, y) from current buffer.
 */
static inline AtomicCell* grid_get_cell(DoubleBufferedGrid* grid, int x, int y) {
    if (x < 0 || x >= grid->width || y < 0 || y >= grid->height) {
        return NULL;
    }
    return &grid->buffers[grid->current_buffer][y * grid->width + x];
}

/**
 * Get cell at (x, y) from next buffer for writing.
 */
static inline AtomicCell* grid_get_next_cell(DoubleBufferedGrid* grid, int x, int y) {
    if (x < 0 || x >= grid->width || y < 0 || y >= grid->height) {
        return NULL;
    }
    return &grid->buffers[1 - grid->current_buffer][y * grid->width + x];
}

// ============================================================================
// Memory Layout Notes for GPU/MPI
// ============================================================================

/*
 * For CUDA/OpenCL acceleration:
 * - AtomicCell arrays can be allocated with cudaMalloc/clCreateBuffer
 * - colony_id is uint32_t - compatible with atomicCAS
 * - Grid is row-major for coalesced memory access
 * - Spread kernel: each thread processes one cell, uses atomicCAS for claims
 * 
 * For MPI/SHMEM:
 * - Use MPI_Win for one-sided access to grid
 * - Atomic operations via MPI_Accumulate with MPI_REPLACE
 * - CAS via MPI_Compare_and_swap
 * - Shape_seed approach means no shape data needs to be transferred
 * 
 * For OpenMP:
 * - #pragma omp parallel for on cell iteration
 * - Atomic operations translate to omp atomic
 */

#endif // FEROX_ATOMIC_TYPES_H
