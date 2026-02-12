/**
 * atomic_sim.c - Atomic-based parallel simulation engine implementation
 * 
 * Lock-free simulation using C11 atomics.
 * Designed for future GPU/MPI/SHMEM acceleration.
 */

#include "atomic_sim.h"
#include "genetics.h"
#include "simulation.h"
#include "../shared/utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// Direction offsets for 8-connectivity spreading  
static const int DX8[] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int DY8[] = {-1, -1, 0, 1, 1, 1, 0, -1};

// ============================================================================
// Thread-local RNG for deterministic parallel processing
// ============================================================================

static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline float rand_float_local(uint32_t* state) {
    return (float)(xorshift32(state) & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

// ============================================================================
// Social/Chemotaxis behavior - neighbor detection and influence
// ============================================================================

/**
 * Calculate social influence multiplier for a spread direction.
 * 
 * Detects nearby colonies and adjusts spread probability:
 * - Positive social_factor: bias toward nearest neighbors (attracted)
 * - Negative social_factor: bias away from nearest neighbors (repelled)
 * 
 * Returns a multiplier 0.5-1.5 to apply to spread probability.
 */
static float calculate_social_influence(
    World* world,
    DoubleBufferedGrid* grid,
    int cell_x, int cell_y,
    int spread_dx, int spread_dy,
    Colony* colony
) {
    const Genome* genome = &colony->genome;
    
    // Skip if no social behavior
    if (fabsf(genome->social_factor) < 0.01f) {
        return 1.0f;
    }
    
    // Calculate detection radius in cells (scale by world size)
    int detect_radius = (int)(genome->detection_range * 
                              (world->width + world->height) / 4.0f);
    if (detect_radius < 5) detect_radius = 5;
    if (detect_radius > 50) detect_radius = 50;
    
    // Find nearest different colonies (sample grid sparsely for performance)
    float nearest_dx = 0, nearest_dy = 0;
    float nearest_dist_sq = (float)(detect_radius * detect_radius + 1);
    int found = 0;
    int step = detect_radius / 4;
    if (step < 2) step = 2;
    
    for (int dy = -detect_radius; dy <= detect_radius && found < genome->max_tracked; dy += step) {
        for (int dx = -detect_radius; dx <= detect_radius && found < genome->max_tracked; dx += step) {
            if (dx == 0 && dy == 0) continue;
            
            int check_x = cell_x + dx;
            int check_y = cell_y + dy;
            
            AtomicCell* check_cell = grid_get_cell(grid, check_x, check_y);
            if (!check_cell) continue;
            
            uint32_t check_colony_id = atomic_load(&check_cell->colony_id);
            if (check_colony_id == 0 || check_colony_id == colony->id) continue;
            
            float dist_sq = (float)(dx * dx + dy * dy);
            if (dist_sq < nearest_dist_sq) {
                nearest_dist_sq = dist_sq;
                nearest_dx = (float)dx;
                nearest_dy = (float)dy;
                found++;
            }
        }
    }
    
    // If no neighbors found, no influence
    if (found == 0) {
        return 1.0f;
    }
    
    // Normalize direction to nearest colony
    float dist = sqrtf(nearest_dist_sq);
    if (dist < 1.0f) dist = 1.0f;
    float norm_dx = nearest_dx / dist;
    float norm_dy = nearest_dy / dist;
    
    // Normalize spread direction
    float spread_len = sqrtf((float)(spread_dx * spread_dx + spread_dy * spread_dy));
    if (spread_len < 0.1f) return 1.0f;
    float spread_norm_dx = (float)spread_dx / spread_len;
    float spread_norm_dy = (float)spread_dy / spread_len;
    
    // Dot product: positive if spreading toward neighbor, negative if away
    float dot = spread_norm_dx * norm_dx + spread_norm_dy * norm_dy;
    
    // Apply social factor:
    // Positive social_factor + positive dot = boost (attracted, moving toward)
    // Negative social_factor + positive dot = reduce (repelled, want to move away)
    float influence = 1.0f + genome->social_factor * dot * 0.4f;
    
    // Clamp to reasonable range
    if (influence < 0.5f) influence = 0.5f;
    if (influence > 1.5f) influence = 1.5f;
    
    return influence;
}

// ============================================================================
// Initialization / Cleanup
// ============================================================================

AtomicWorld* atomic_world_create(World* world, ThreadPool* pool, int thread_count) {
    if (!world || !pool || thread_count <= 0) {
        return NULL;
    }
    
    AtomicWorld* aworld = (AtomicWorld*)malloc(sizeof(AtomicWorld));
    if (!aworld) return NULL;
    
    memset(aworld, 0, sizeof(AtomicWorld));
    aworld->world = world;
    aworld->pool = pool;
    aworld->thread_count = thread_count;
    
    int grid_size = world->width * world->height;
    
    // Allocate double-buffered grid
    aworld->grid.width = world->width;
    aworld->grid.height = world->height;
    aworld->grid.current_buffer = 0;
    
    aworld->grid.buffers[0] = (AtomicCell*)calloc(grid_size, sizeof(AtomicCell));
    aworld->grid.buffers[1] = (AtomicCell*)calloc(grid_size, sizeof(AtomicCell));
    
    if (!aworld->grid.buffers[0] || !aworld->grid.buffers[1]) {
        free(aworld->grid.buffers[0]);
        free(aworld->grid.buffers[1]);
        free(aworld);
        return NULL;
    }
    
    // Allocate colony stats (over-allocate significantly for colony divisions)
    // Colony IDs can grow large due to divisions, so use a generous initial size
    aworld->max_colonies = 4096;  // Support many divisions
    aworld->colony_stats = (AtomicColonyStats*)calloc(aworld->max_colonies, sizeof(AtomicColonyStats));
    if (!aworld->colony_stats) {
        free(aworld->grid.buffers[0]);
        free(aworld->grid.buffers[1]);
        free(aworld);
        return NULL;
    }
    
    // Allocate per-thread RNG seeds
    aworld->thread_seeds = (uint32_t*)malloc(thread_count * sizeof(uint32_t));
    if (!aworld->thread_seeds) {
        free(aworld->colony_stats);
        free(aworld->grid.buffers[0]);
        free(aworld->grid.buffers[1]);
        free(aworld);
        return NULL;
    }
    
    // Initialize thread seeds with different values
    for (int i = 0; i < thread_count; i++) {
        aworld->thread_seeds[i] = (uint32_t)(12345 + i * 7919);  // Prime offset
    }
    
    // Sync from world
    atomic_world_sync_from_world(aworld);
    
    return aworld;
}

void atomic_world_destroy(AtomicWorld* aworld) {
    if (!aworld) return;
    
    free(aworld->thread_seeds);
    free(aworld->colony_stats);
    free(aworld->grid.buffers[0]);
    free(aworld->grid.buffers[1]);
    free(aworld);
}

void atomic_world_sync_from_world(AtomicWorld* aworld) {
    if (!aworld || !aworld->world) return;
    
    World* world = aworld->world;
    
    // Check if we need to expand colony stats array
    // Find the max colony ID in use
    uint32_t max_id = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].id > max_id) {
            max_id = world->colonies[i].id;
        }
    }
    
    // Resize if needed
    if (max_id >= aworld->max_colonies) {
        size_t new_max = (max_id + 1) * 2;  // Double the required size
        AtomicColonyStats* new_stats = (AtomicColonyStats*)calloc(new_max, sizeof(AtomicColonyStats));
        if (new_stats) {
            // Copy existing stats
            for (size_t i = 0; i < aworld->max_colonies; i++) {
                new_stats[i] = aworld->colony_stats[i];
            }
            free(aworld->colony_stats);
            aworld->colony_stats = new_stats;
            aworld->max_colonies = new_max;
        } else {
            // Allocation failed - this is critical, log error
            // Continue with existing array but skip colonies with IDs >= max_colonies
            fprintf(stderr, "Warning: Failed to expand colony_stats array (new_max=%zu)\n", new_max);
        }
    }
    
    AtomicCell* current = grid_current(&aworld->grid);
    AtomicCell* next = grid_next(&aworld->grid);
    
    // Clear only cell_count (NOT max - max should never decrease)
    for (size_t i = 0; i < aworld->max_colonies; i++) {
        atomic_store(&aworld->colony_stats[i].cell_count, 0);
    }
    
    // Copy cell data and count populations
    for (int i = 0; i < world->width * world->height; i++) {
        Cell* cell = &world->cells[i];
        
        atomic_store(&current[i].colony_id, cell->colony_id);
        atomic_store(&current[i].age, cell->age);
        current[i].is_border = cell->is_border ? 1 : 0;
        
        // Also init next buffer
        atomic_store(&next[i].colony_id, cell->colony_id);
        atomic_store(&next[i].age, cell->age);
        next[i].is_border = cell->is_border ? 1 : 0;
        
        // Count population
        if (cell->colony_id != 0 && cell->colony_id < aworld->max_colonies) {
            atomic_fetch_add(&aworld->colony_stats[cell->colony_id].cell_count, 1);
        }
    }
    
    // Update max counts from world (only increase, never decrease)
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (colony->id < aworld->max_colonies) {
            int64_t current_max = atomic_load(&aworld->colony_stats[colony->id].max_cell_count);
            int64_t world_max = (int64_t)colony->max_cell_count;
            if (world_max > current_max) {
                atomic_store(&aworld->colony_stats[colony->id].max_cell_count, world_max);
            }
        }
    }
}

void atomic_world_sync_to_world(AtomicWorld* aworld) {
    if (!aworld || !aworld->world) return;
    
    World* world = aworld->world;
    AtomicCell* current = grid_current(&aworld->grid);
    
    // Copy cell data back
    for (int i = 0; i < world->width * world->height; i++) {
        world->cells[i].colony_id = atomic_load(&current[i].colony_id);
        world->cells[i].age = atomic_load(&current[i].age);
        world->cells[i].is_border = current[i].is_border != 0;
    }
    
    // Update colony stats
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (colony->id < aworld->max_colonies) {
            colony->cell_count = (size_t)atomic_load(&aworld->colony_stats[colony->id].cell_count);
            
            int64_t max = atomic_load(&aworld->colony_stats[colony->id].max_cell_count);
            if ((size_t)max > colony->max_cell_count) {
                colony->max_cell_count = (size_t)max;
            }
            
            // Mark inactive if dead
            if (colony->cell_count == 0 && colony->active) {
                colony->active = false;
            }
        }
    }
}

// ============================================================================
// Thread Work Functions
// ============================================================================

static void spread_task_func(void* arg) {
    AtomicRegionWork* work = (AtomicRegionWork*)arg;
    atomic_spread_region(work);
    free(work);
}

static void age_task_func(void* arg) {
    AtomicRegionWork* work = (AtomicRegionWork*)arg;
    atomic_age_region(work);
    free(work);
}

void atomic_spread_region(AtomicRegionWork* work) {
    AtomicWorld* aworld = work->aworld;
    World* world = aworld->world;
    DoubleBufferedGrid* grid = &aworld->grid;
    
    // Thread-local RNG
    uint32_t rng_state = aworld->thread_seeds[work->thread_id];
    
    // Process each cell in region
    for (int y = work->start_y; y < work->end_y; y++) {
        for (int x = work->start_x; x < work->end_x; x++) {
            AtomicCell* cell = grid_get_cell(grid, x, y);
            if (!cell) continue;
            
            uint32_t colony_id = atomic_load(&cell->colony_id);
            if (colony_id == 0) continue;  // Empty cell
            
            // Skip cells with colony IDs beyond our tracking capacity
            // This can happen if colony_stats resize failed
            if (colony_id >= aworld->max_colonies) continue;
            
            // Don't spread from cells with age=0 (just claimed this tick)
            // This prevents cascade spreading within a single tick
            uint8_t age = atomic_load(&cell->age);
            if (age == 0) continue;
            
            // Get colony for spread parameters
            Colony* colony = world_get_colony(world, colony_id);
            if (!colony || !colony->active) continue;
            
            // Try to spread to neighbors using 8-connectivity
            for (int d = 0; d < 8; d++) {
                int dx = DX8[d];
                int dy = DY8[d];
                int nx = x + dx;
                int ny = y + dy;
                
                AtomicCell* neighbor = grid_get_cell(grid, nx, ny);
                if (!neighbor) continue;
                
                uint32_t neighbor_colony = atomic_load(&neighbor->colony_id);
                
                // Calculate base spread probability
                float spread_prob = colony->genome.spread_rate * 
                                   colony->genome.metabolism *
                                   colony->genome.spread_weights[d];
                
                // Apply social influence (chemotaxis toward/away from neighbors)
                float social_mult = calculate_social_influence(
                    world, grid, x, y, dx, dy, colony);
                spread_prob *= social_mult;
                
                if (neighbor_colony == 0) {
                    // Empty cell - try to spread
                    if (rand_float_local(&rng_state) < spread_prob) {
                        // Atomic CAS: try to claim the cell
                        // Only succeeds if cell is still empty
                        if (atomic_cell_try_claim(neighbor, 0, colony_id)) {
                            atomic_store(&neighbor->age, 0);
                            // Update population counts atomically
                            if (colony_id < aworld->max_colonies) {
                                atomic_stats_add_cell(&aworld->colony_stats[colony_id]);
                            }
                        }
                    }
                } else if (neighbor_colony != colony_id) {
                    // Colonies cannot directly attack each other
                    // They compete by racing to fill empty space
                    // This promotes coexistence and diverse ecosystems
                    continue;
                }
            }
        }
    }
    
    // Save RNG state back (for reproducibility)
    aworld->thread_seeds[work->thread_id] = rng_state;
}

void atomic_age_region(AtomicRegionWork* work) {
    DoubleBufferedGrid* grid = &work->aworld->grid;
    
    for (int y = work->start_y; y < work->end_y; y++) {
        for (int x = work->start_x; x < work->end_x; x++) {
            AtomicCell* cell = grid_get_cell(grid, x, y);
            if (cell && atomic_load(&cell->colony_id) != 0) {
                atomic_cell_age(cell);
            }
        }
    }
}

// ============================================================================
// Parallel Phases
// ============================================================================

static void submit_region_tasks(AtomicWorld* aworld, void (*task_func)(void*)) {
    int regions_per_side = aworld->thread_count > 4 ? 4 : 2;
    int regions_x = regions_per_side;
    int regions_y = regions_per_side;
    
    int region_width = aworld->grid.width / regions_x;
    int region_height = aworld->grid.height / regions_y;
    
    int thread_id = 0;
    
    for (int ry = 0; ry < regions_y; ry++) {
        for (int rx = 0; rx < regions_x; rx++) {
            AtomicRegionWork* work = (AtomicRegionWork*)malloc(sizeof(AtomicRegionWork));
            if (!work) continue;
            
            work->aworld = aworld;
            work->start_x = rx * region_width;
            work->start_y = ry * region_height;
            work->end_x = (rx == regions_x - 1) ? aworld->grid.width : (rx + 1) * region_width;
            work->end_y = (ry == regions_y - 1) ? aworld->grid.height : (ry + 1) * region_height;
            work->thread_id = thread_id % aworld->thread_count;
            thread_id++;
            
            threadpool_submit(aworld->pool, task_func, work);
        }
    }
}

void atomic_age(AtomicWorld* aworld) {
    if (!aworld) return;
    submit_region_tasks(aworld, age_task_func);
}

void atomic_spread(AtomicWorld* aworld) {
    if (!aworld) return;
    submit_region_tasks(aworld, spread_task_func);
}

void atomic_barrier(AtomicWorld* aworld) {
    if (!aworld) return;
    threadpool_wait(aworld->pool);
}

// ============================================================================
// Main Tick Function
// ============================================================================

void atomic_tick(AtomicWorld* aworld) {
    if (!aworld || !aworld->world) return;
    
    World* world = aworld->world;
    
    // === Parallel Phase ===
    
    // Age all cells in parallel
    atomic_age(aworld);
    atomic_barrier(aworld);
    
    // Spread colonies in parallel using atomic CAS
    atomic_spread(aworld);
    atomic_barrier(aworld);
    
    // === Serial Phase ===
    // Sync atomic state back to regular world for complex operations
    atomic_world_sync_to_world(aworld);
    
    // Mutations (per-colony, serial)
    simulation_mutate(world);
    
    // Division detection (requires flood-fill, serial)
    simulation_check_divisions(world);
    
    // Recombination (serial)
    simulation_check_recombinations(world);
    
    // Update colony stats (wobble animation, shape evolution)
    simulation_update_colony_stats(world);
    
    // Sync any changes back to atomic world
    atomic_world_sync_from_world(aworld);
    
    world->tick++;
}

// ============================================================================
// Statistics
// ============================================================================

int64_t atomic_get_population(AtomicWorld* aworld, uint32_t colony_id) {
    if (!aworld || colony_id >= aworld->max_colonies) return 0;
    return atomic_stats_get_count(&aworld->colony_stats[colony_id]);
}

int64_t atomic_get_max_population(AtomicWorld* aworld, uint32_t colony_id) {
    if (!aworld || colony_id >= aworld->max_colonies) return 0;
    return atomic_stats_get_max(&aworld->colony_stats[colony_id]);
}
