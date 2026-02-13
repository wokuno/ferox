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
#include "../shared/names.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// Direction offsets for 8-connectivity spreading  
static const int DX8[] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int DY8[] = {-1, -1, 0, 1, 1, 1, 0, -1};
// Diagonal probability correction: 1.0 for cardinal, 1/sqrt(2) for diagonal
static const float DIR8_WEIGHT[] = {1.0f, 0.7071f, 1.0f, 0.7071f, 1.0f, 0.7071f, 1.0f, 0.7071f};

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
 * Uses scent field to detect nearby colonies and adjust spread probability:
 * - High aggression: attracted to enemy scent (hunt them)
 * - High defense: repelled by enemy scent (avoid conflict)
 * - Also considers social_factor for general neighbor attraction/repulsion
 * 
 * Returns a multiplier 0.3-2.0 to apply to spread probability.
 */
static float calculate_social_influence(
    World* world,
    DoubleBufferedGrid* grid,
    int cell_x, int cell_y,
    int spread_dx, int spread_dy,
    Colony* colony
) {
    const Genome* genome = &colony->genome;
    float influence = 1.0f;
    
    // Check scent at target location
    int nx = cell_x + spread_dx;
    int ny = cell_y + spread_dy;
    if (nx >= 0 && nx < world->width && ny >= 0 && ny < world->height && world->signals) {
        int idx = ny * world->width + nx;
        float scent = world->signals[idx];
        uint32_t source = world->signal_source ? world->signal_source[idx] : 0;
        
        if (scent > 0.01f && source > 0) {
            if (source == colony->id) {
                // Self-scent: avoid overcrowding unless high density tolerance
                influence *= 1.0f - scent * (1.0f - genome->density_tolerance) * 0.3f;
            } else {
                // Enemy scent: react based on aggression vs defense
                float aggression = genome->aggression;
                float defense = genome->defense_priority;
                float sensitivity = genome->signal_sensitivity;
                
                // reaction > 0: attracted to enemy (aggressive hunting)
                // reaction < 0: repelled by enemy (defensive avoidance)
                float reaction = (aggression - defense) * sensitivity;
                influence *= 1.0f + reaction * scent * 0.6f;
            }
        }
    }
    
    // Also check direct neighbor detection for immediate threats
    if (fabsf(genome->social_factor) > 0.01f) {
        // Calculate detection radius in cells (scale by world size)
        int detect_radius = (int)(genome->detection_range * 
                                  (world->width + world->height) / 4.0f);
        if (detect_radius < 5) detect_radius = 5;
        if (detect_radius > 30) detect_radius = 30;
    
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
        
        // Apply social factor based on nearest neighbor direction
        if (found > 0) {
            float dist = sqrtf(nearest_dist_sq);
            if (dist >= 1.0f) {
                float norm_dx = nearest_dx / dist;
                float norm_dy = nearest_dy / dist;
                
                float spread_len = sqrtf((float)(spread_dx * spread_dx + spread_dy * spread_dy));
                if (spread_len >= 0.1f) {
                    float spread_norm_dx = (float)spread_dx / spread_len;
                    float spread_norm_dy = (float)spread_dy / spread_len;
                    
                    // Dot product: positive if spreading toward neighbor
                    float dot = spread_norm_dx * norm_dx + spread_norm_dy * norm_dy;
                    influence *= 1.0f + genome->social_factor * dot * 0.4f;
                }
            }
        }
    }
    
    // Clamp to reasonable range
    if (influence < 0.3f) influence = 0.3f;
    if (influence > 2.0f) influence = 2.0f;
    
    return influence;
}

// ============================================================================
// Long-run dynamics helpers (serial, post-parallel)
// ============================================================================

static void atomic_apply_cell_turnover(World* world) {
    if (!world) return;

    for (int i = 0; i < world->width * world->height; i++) {
        Cell* cell = &world->cells[i];
        if (cell->colony_id == 0) continue;

        Colony* colony = world_get_colony(world, cell->colony_id);
        if (!colony || !colony->active) continue;

        float death_chance = 0.0015f;

        // Larger colonies are harder to supply internally.
        death_chance += utils_clamp_f((float)colony->cell_count / 60000.0f, 0.0f, 0.012f);

        if (world->nutrients) {
            float nutrient = world->nutrients[i];
            if (nutrient < 0.25f) {
                death_chance += (0.25f - nutrient) * 0.08f *
                                (1.0f - colony->genome.efficiency * 0.7f);
            }
        }

        if (world->toxins) {
            float toxin = world->toxins[i];
            if (toxin > 0.25f) {
                death_chance += (toxin - 0.25f) * 0.10f *
                                (1.0f - colony->genome.toxin_resistance);
            }
        }

        // Interior decay pressure to keep colonies breathing/churning.
        if (!cell->is_border && colony->cell_count > 80) {
            death_chance *= 1.25f;
        }

        // Persister-like dormancy: better survival, weaker expansion elsewhere.
        if (colony->is_dormant) {
            float dormancy_factor = 0.50f - colony->genome.dormancy_resistance * 0.30f;
            death_chance *= utils_clamp_f(dormancy_factor, 0.18f, 0.50f);
        }

        if (cell->age > 140) {
            death_chance += ((float)cell->age - 140.0f) * 0.0008f;
        }

        death_chance = utils_clamp_f(death_chance, 0.0f, 0.35f);
        if (rand_float() < death_chance) {
            if (world->nutrients) {
                world->nutrients[i] = utils_clamp_f(world->nutrients[i] + 0.2f, 0.0f, 1.0f);
            }
            cell->colony_id = 0;
            cell->age = 0;
            cell->is_border = false;
        }
    }
}

static void atomic_spawn_dynamic_colonies(World* world) {
    if (!world) return;

    int active_colonies = 0;
    int total_cells = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) {
            active_colonies++;
            total_cells += (int)world->colonies[i].cell_count;
        }
    }

    int world_size = world->width * world->height;
    float empty_ratio = 1.0f - (float)total_cells / (float)world_size;
    bool force_spawn = active_colonies < 4;

    float spawn_chance = 0.04f + empty_ratio * 0.20f;
    if (active_colonies < 12) spawn_chance += 0.18f;

    if ((active_colonies < 240 && rand_float() < spawn_chance) || force_spawn) {
        for (int attempts = 0; attempts < 80; attempts++) {
            int x = rand() % world->width;
            int y = rand() % world->height;
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id != 0) continue;

            Colony colony;
            memset(&colony, 0, sizeof(Colony));
            colony.genome = genome_create_random();
            colony.color = colony.genome.body_color;
            colony.cell_count = 1;
            colony.max_cell_count = 1;
            colony.active = true;
            colony.parent_id = 0;
            colony.shape_seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
            colony.wobble_phase = (float)(rand() % 628) / 100.0f;
            colony.shape_evolution = (float)(rand() % 1000) / 1000.0f;
            colony.state = COLONY_STATE_NORMAL;
            colony.is_dormant = false;
            colony.stress_level = 0.0f;
            colony.biofilm_strength = 0.0f;
            colony.drift_x = 0.0f;
            colony.drift_y = 0.0f;
            colony.signal_strength = 0.0f;
            colony.last_population = 1;
            generate_scientific_name(colony.name, sizeof(colony.name));

            uint32_t id = world_add_colony(world, colony);
            if (id > 0) {
                cell->colony_id = id;
                cell->age = 0;
                cell->is_border = true;
            }
            break;
        }
    }
}

static void atomic_update_colony_behavior(World* world) {
    if (!world) return;

    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        if (colony->cell_count == 0) {
            colony->active = false;
            continue;
        }

        float colony_density = (float)colony->cell_count / (float)(world->width * world->height);
        float scaled_density = utils_clamp_f(colony_density * 900.0f, 0.0f, 1.0f);
        float ai_input = scaled_density * colony->genome.signal_emission *
                         (0.7f + colony->genome.signal_sensitivity * 0.6f);
        if (colony->is_dormant) ai_input *= 0.6f;
        colony->signal_strength = utils_clamp_f(
            colony->signal_strength * 0.92f + ai_input * 0.35f, 0.0f, 1.0f
        );

        float quorum_activation = 0.0f;
        float threshold = colony->genome.quorum_threshold;
        if (colony->signal_strength > threshold) {
            quorum_activation = utils_clamp_f(
                (colony->signal_strength - threshold) / (1.0f - threshold + 0.001f),
                0.0f, 1.0f
            );
        }

        if (quorum_activation > 0.0f && colony->genome.biofilm_tendency > 0.3f) {
            colony->biofilm_strength = utils_clamp_f(
                colony->biofilm_strength + 0.002f + quorum_activation * 0.004f,
                0.0f, 1.0f
            );
        }

        colony->stress_level = utils_clamp_f(colony->stress_level - 0.002f, 0.0f, 1.0f);

        if (colony->genome.biofilm_investment > 0.3f) {
            float target_biofilm = colony->genome.biofilm_investment * colony->genome.biofilm_tendency;
            if (colony->biofilm_strength < target_biofilm) {
                colony->biofilm_strength = utils_clamp_f(colony->biofilm_strength + 0.01f, 0.0f, 1.0f);
            }
        }
        colony->biofilm_strength = utils_clamp_f(colony->biofilm_strength - 0.002f, 0.0f, 1.0f);

        if (colony->stress_level > colony->genome.sporulation_threshold &&
            colony->genome.dormancy_threshold > 0.3f) {
            colony->state = COLONY_STATE_DORMANT;
            colony->is_dormant = true;
        } else if (colony->stress_level > 0.5f) {
            colony->state = COLONY_STATE_STRESSED;
            colony->is_dormant = false;
        } else {
            colony->state = COLONY_STATE_NORMAL;
            colony->is_dormant = false;
        }

        colony->shape_evolution += 0.002f;
        if (colony->shape_evolution > 100.0f) colony->shape_evolution -= 100.0f;
    }
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
    
    // Preallocate region work items
    int regions_per_side = thread_count > 4 ? 4 : 2;
    aworld->region_work_count = regions_per_side * regions_per_side;
    aworld->region_work = (AtomicRegionWork*)calloc(aworld->region_work_count, sizeof(AtomicRegionWork));
    if (!aworld->region_work) {
        free(aworld->thread_seeds);
        free(aworld->colony_stats);
        free(aworld->grid.buffers[0]);
        free(aworld->grid.buffers[1]);
        free(aworld);
        return NULL;
    }
    
    // Sync from world
    atomic_world_sync_from_world(aworld);
    
    return aworld;
}

void atomic_world_destroy(AtomicWorld* aworld) {
    if (!aworld) return;
    
    free(aworld->region_work);
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
    
    // Copy cell data only where changed and count populations
    for (int i = 0; i < world->width * world->height; i++) {
        Cell* cell = &world->cells[i];
        
        uint32_t cur_colony = atomic_load(&current[i].colony_id);
        uint8_t cur_age = atomic_load(&current[i].age);
        uint8_t cur_border = current[i].is_border;
        uint8_t cell_border = cell->is_border ? 1 : 0;
        
        if (cur_colony != cell->colony_id || cur_age != cell->age || cur_border != cell_border) {
            atomic_store(&current[i].colony_id, cell->colony_id);
            atomic_store(&current[i].age, cell->age);
            current[i].is_border = cell_border;
            
            atomic_store(&next[i].colony_id, cell->colony_id);
            atomic_store(&next[i].age, cell->age);
            next[i].is_border = cell_border;
        }
        
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
    
    // Copy cell data back only where changed
    for (int i = 0; i < world->width * world->height; i++) {
        uint32_t atomic_colony = atomic_load(&current[i].colony_id);
        uint8_t atomic_age = atomic_load(&current[i].age);
        bool atomic_border = current[i].is_border != 0;
        
        if (world->cells[i].colony_id != atomic_colony ||
            world->cells[i].age != atomic_age ||
            world->cells[i].is_border != atomic_border) {
            world->cells[i].colony_id = atomic_colony;
            world->cells[i].age = atomic_age;
            world->cells[i].is_border = atomic_border;
        }
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
}

static void age_task_func(void* arg) {
    AtomicRegionWork* work = (AtomicRegionWork*)arg;
    atomic_age_region(work);
}

void atomic_spread_region(AtomicRegionWork* work) {
    AtomicWorld* aworld = work->aworld;
    World* world = aworld->world;
    DoubleBufferedGrid* grid = &aworld->grid;
    AtomicCell* current = grid_current(grid);
    const int width = grid->width;
    
    // Thread-local RNG
    uint32_t rng_state = aworld->thread_seeds[work->thread_id];
    
    // Process each cell in region
    for (int y = work->start_y; y < work->end_y; y++) {
        int idx = y * width + work->start_x;
        for (int x = work->start_x; x < work->end_x; x++) {
            AtomicCell* cell = &current[idx++];
            
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
                
                // Calculate base spread probability with diagonal correction
                float spread_prob = colony->genome.spread_rate * 
                                   colony->genome.metabolism *
                                   colony->genome.spread_weights[d] *
                                   DIR8_WEIGHT[d];  // 1/√2 for diagonals
                
                // Per-cell stochastic noise for organic/irregular edges
                float noise = 0.6f + rand_float_local(&rng_state) * 0.8f;
                spread_prob *= noise;
                
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
    AtomicCell* current = grid_current(grid);
    const int width = grid->width;
    
    for (int y = work->start_y; y < work->end_y; y++) {
        int idx = y * width + work->start_x;
        for (int x = work->start_x; x < work->end_x; x++) {
            AtomicCell* cell = &current[idx++];
            if (atomic_load(&cell->colony_id) != 0) {
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
    
    int task_idx = 0;
    
    for (int ry = 0; ry < regions_y; ry++) {
        for (int rx = 0; rx < regions_x; rx++) {
            AtomicRegionWork* work = &aworld->region_work[task_idx];
            
            work->aworld = aworld;
            work->start_x = rx * region_width;
            work->start_y = ry * region_height;
            work->end_x = (rx == regions_x - 1) ? aworld->grid.width : (rx + 1) * region_width;
            work->end_y = (ry == regions_y - 1) ? aworld->grid.height : (ry + 1) * region_height;
            work->thread_id = task_idx % aworld->thread_count;
            task_idx++;
            
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
    
    // Resource and chemical fields drive long-term dynamics.
    simulation_update_nutrients(world);

    // Update scent field (colonies emit and scent diffuses)
    simulation_update_scents(world);

    // Border combat keeps territories fluid and competitive.
    simulation_resolve_combat(world);

    // Continuous turnover prevents late-game lockup/static equilibria.
    atomic_apply_cell_turnover(world);
    
    // Mutations (per-colony, serial)
    simulation_mutate(world);
    
    // Division detection (requires flood-fill, serial)
    simulation_check_divisions(world);
    
    // Recombination (serial)
    simulation_check_recombinations(world);

    // Keep introducing new lineages so ecosystem stays volatile.
    atomic_spawn_dynamic_colonies(world);
    
    // Update colony stats (wobble animation, shape evolution)
    simulation_update_colony_stats(world);
    atomic_update_colony_behavior(world);
    
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
