/**
 * parallel.h - Parallel world update orchestration
 * Part of Phase 3: Threading & Concurrency
 */

#ifndef PARALLEL_H
#define PARALLEL_H

#include "threadpool.h"
#include <pthread.h>

// Include types.h for World definition
#include "../shared/types.h"

// Forward declare PendingBuffer from simulation.h
typedef struct PendingBuffer PendingBuffer;

// Region structure for grid partitioning
typedef struct Region {
    int start_x, start_y;
    int end_x, end_y;
} Region;

// Parallel update context
typedef struct ParallelContext {
    ThreadPool* pool;
    World* world;
    Region* regions;
    int region_count;
    int regions_x;
    int regions_y;
    
    // Pending buffers for spread phase (one per region)
    PendingBuffer** pending_buffers;
} ParallelContext;

/**
 * Create a parallel context for updating the world.
 * Divides the world into a grid of regions for parallel processing.
 * @param pool The thread pool to use for task execution
 * @param world The world to update (can be NULL for testing)
 * @param regions_x Number of regions in the X dimension
 * @param regions_y Number of regions in the Y dimension
 * @return Pointer to the new ParallelContext, or NULL on failure
 */
ParallelContext* parallel_create(ThreadPool* pool, World* world, int regions_x, int regions_y);

/**
 * Destroy the parallel context and free all resources.
 * Does not destroy the associated thread pool or world.
 * @param ctx The parallel context to destroy
 */
void parallel_destroy(ParallelContext* ctx);

/**
 * Process colony spreading in parallel across all regions.
 * Each region is processed by a separate task in the thread pool.
 * @param ctx The parallel context
 */
void parallel_spread(ParallelContext* ctx);

/**
 * Process mutations in parallel across all regions.
 * Each region is processed by a separate task in the thread pool.
 * @param ctx The parallel context
 */
void parallel_mutate(ParallelContext* ctx);

/**
 * Barrier synchronization between phases.
 * Waits for all pending tasks to complete before returning.
 * @param ctx The parallel context
 */
void parallel_barrier(ParallelContext* ctx);

/**
 * Initialize region bounds based on world dimensions.
 * Must be called after world is attached if world was NULL during creation.
 * @param ctx The parallel context
 * @param world_width The width of the world grid
 * @param world_height The height of the world grid
 */
void parallel_init_regions(ParallelContext* ctx, int world_width, int world_height);

/**
 * Run a complete parallel simulation tick.
 * Coordinates all phases: aging, spreading, mutations, divisions, recombinations.
 * @param ctx The parallel context
 */
void parallel_tick(ParallelContext* ctx);

/**
 * Age cells in parallel across all regions.
 * @param ctx The parallel context
 */
void parallel_age(ParallelContext* ctx);

#endif // PARALLEL_H
