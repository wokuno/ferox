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
#include <time.h>

// Direction offsets for 8-connectivity spreading  
static const int DX8[] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int DY8[] = {-1, -1, 0, 1, 1, 1, 0, -1};

enum {
    ATOMIC_PHASE_IDLE = 0,
    ATOMIC_PHASE_AGE = 1,
    ATOMIC_PHASE_SPREAD = 2,
    ATOMIC_PHASE_SPREAD_FRONTIER = 3
};

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

static double atomic_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static int atomic_parse_env_int(const char* name, int default_value, int min_value, int max_value) {
    const char* raw = getenv(name);
    if (!raw || !*raw) {
        return default_value;
    }

    char* end = NULL;
    long value = strtol(raw, &end, 10);
    if (end == raw || (end && *end != '\0')) {
        return default_value;
    }

    if (value < min_value) value = min_value;
    if (value > max_value) value = max_value;
    return (int)value;
}

static bool atomic_parse_env_bool(const char* name, bool default_value) {
    const char* raw = getenv(name);
    if (!raw || !*raw) {
        return default_value;
    }

    if (strcmp(raw, "1") == 0 || strcmp(raw, "true") == 0 || strcmp(raw, "yes") == 0 || strcmp(raw, "on") == 0) {
        return true;
    }
    if (strcmp(raw, "0") == 0 || strcmp(raw, "false") == 0 || strcmp(raw, "no") == 0 || strcmp(raw, "off") == 0) {
        return false;
    }

    return default_value;
}

static bool calculate_social_vector(
    World* world,
    DoubleBufferedGrid* grid,
    int cell_x,
    int cell_y,
    Colony* colony,
    float* out_norm_dx,
    float* out_norm_dy
);

static float calculate_social_influence_for_direction(
    int spread_dx,
    int spread_dy,
    float social_factor,
    float neighbor_norm_dx,
    float neighbor_norm_dy
);

static void compute_region_grid(int width, int height, int thread_count, int* out_x, int* out_y) {
    if (width <= 0 || height <= 0 || thread_count <= 0) {
        *out_x = 1;
        *out_y = 1;
        return;
    }

    int tile_x = (width + 31) / 32;
    int tile_y = (height + 31) / 32;
    int max_regions = tile_x * tile_y;
    if (max_regions < 1) {
        max_regions = 1;
    }

    int target_regions = thread_count * 4;
    if (target_regions < thread_count) {
        target_regions = thread_count;
    }
    if (target_regions > max_regions) {
        target_regions = max_regions;
    }

    double aspect = (double)width / (double)height;
    int regions_x = (int)sqrt((double)target_regions * aspect);
    if (regions_x < 1) {
        regions_x = 1;
    }
    if (regions_x > width) {
        regions_x = width;
    }

    int regions_y = (target_regions + regions_x - 1) / regions_x;
    if (regions_y < 1) {
        regions_y = 1;
    }
    if (regions_y > height) {
        regions_y = height;
    }

    while (regions_x * regions_y < target_regions && regions_x < width) {
        regions_x++;
    }

    *out_x = regions_x;
    *out_y = regions_y;
}

static int atomic_alloc_spread_tracking(AtomicWorld* aworld, size_t max_colonies) {
    int slot_capacity = aworld->region_count;
    if (aworld->thread_count > slot_capacity) {
        slot_capacity = aworld->thread_count;
    }
    if (slot_capacity < 1) {
        slot_capacity = 1;
    }

    size_t total_slots = (size_t)slot_capacity * max_colonies;

    int32_t* deltas = (int32_t*)calloc(total_slots, sizeof(int32_t));
    uint32_t* touched_ids = (uint32_t*)calloc(total_slots, sizeof(uint32_t));
    uint32_t* touched_counts = (uint32_t*)calloc((size_t)slot_capacity, sizeof(uint32_t));

    if (!deltas || !touched_ids || !touched_counts) {
        free(deltas);
        free(touched_ids);
        free(touched_counts);
        return -1;
    }

    free(aworld->spread_deltas);
    free(aworld->spread_touched_ids);
    free(aworld->spread_touched_counts);

    aworld->spread_deltas = deltas;
    aworld->spread_touched_ids = touched_ids;
    aworld->spread_touched_counts = touched_counts;
    aworld->spread_state.spread_slot_capacity = slot_capacity;
    aworld->spread_state.spread_slots_used = aworld->region_count;
    return 0;
}

static int64_t atomic_apply_spread_deltas_internal(AtomicWorld* aworld) {
    if (!aworld || !aworld->spread_deltas || !aworld->spread_touched_ids || !aworld->spread_touched_counts) {
        return 0;
    }

    int64_t total_applied = 0;

    int slots_used = aworld->spread_state.spread_slots_used;
    if (slots_used < 0 || slots_used > aworld->spread_state.spread_slot_capacity) {
        slots_used = aworld->spread_state.spread_slot_capacity;
    }

    for (int slot = 0; slot < slots_used; slot++) {
        size_t slot_base = (size_t)slot * aworld->max_colonies;
        int32_t* region_deltas = &aworld->spread_deltas[slot_base];
        uint32_t* region_touched = &aworld->spread_touched_ids[slot_base];
        uint32_t touched_count = aworld->spread_touched_counts[slot];

        for (uint32_t i = 0; i < touched_count; i++) {
            uint32_t colony_id = region_touched[i];
            int32_t delta = region_deltas[colony_id];
            if (delta <= 0) {
                region_deltas[colony_id] = 0;
                continue;
            }

            AtomicColonyStats* stats = &aworld->colony_stats[colony_id];
            int64_t new_count = atomic_fetch_add_explicit(&stats->cell_count, (int64_t)delta, memory_order_relaxed) + (int64_t)delta;
            int64_t old_max = atomic_load_explicit(&stats->max_cell_count, memory_order_relaxed);
            while (new_count > old_max) {
                if (atomic_compare_exchange_weak_explicit(
                        &stats->max_cell_count,
                        &old_max,
                        new_count,
                        memory_order_relaxed,
                        memory_order_relaxed)) {
                    break;
                }
            }

            total_applied += (int64_t)delta;

            region_deltas[colony_id] = 0;
        }

        aworld->spread_touched_counts[slot] = 0;
    }

    return total_applied;
}

static bool atomic_cell_has_empty_neighbor(const AtomicCell* cells, int width, int height, int idx) {
    int x = idx % width;
    int y = idx / width;

    for (int d = 0; d < 8; d++) {
        int nx = x + DX8[d];
        int ny = y + DY8[d];
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
            continue;
        }

        size_t nidx = (size_t)ny * (size_t)width + (size_t)nx;
        if (atomic_load_explicit(&cells[nidx].colony_id, memory_order_relaxed) == 0) {
            return true;
        }
    }

    return false;
}

static float atomic_direction_alignment(int dx, int dy, float drift_x, float drift_y) {
    float dir_x = (float)dx;
    float dir_y = (float)dy;
    float dir_len = sqrtf(dir_x * dir_x + dir_y * dir_y);
    float drift_len = sqrtf(drift_x * drift_x + drift_y * drift_y);
    if (dir_len < 0.0001f || drift_len < 0.0001f) {
        return 0.0f;
    }

    return (dir_x * drift_x + dir_y * drift_y) / (dir_len * drift_len);
}

static void atomic_count_neighbor_mix(
    const AtomicCell* cells,
    int width,
    int height,
    int x,
    int y,
    uint32_t colony_id,
    int* friendly,
    int* enemy,
    int* empty,
    int* total
) {
    if (friendly) *friendly = 0;
    if (enemy) *enemy = 0;
    if (empty) *empty = 0;
    if (total) *total = 0;

    for (int d = 0; d < 8; d++) {
        int nx = x + DX8[d];
        int ny = y + DY8[d];
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
            continue;
        }

        size_t nidx = (size_t)ny * (size_t)width + (size_t)nx;
        uint32_t neighbor_colony = atomic_load_explicit(&cells[nidx].colony_id, memory_order_relaxed);
        if (total) (*total)++;
        if (neighbor_colony == 0) {
            if (empty) (*empty)++;
        } else if (neighbor_colony == colony_id) {
            if (friendly) (*friendly)++;
        } else {
            if (enemy) (*enemy)++;
        }
    }
}

static float atomic_calculate_behavioral_spread_modifier(
    AtomicWorld* aworld,
    Colony* colony,
    uint32_t colony_id,
    int source_x,
    int source_y,
    int target_x,
    int target_y,
    int dir_index,
    int source_friendly,
    int source_enemy,
    int source_empty,
    int source_total
) {
    World* world = aworld->world;
    int target_idx = target_y * world->width + target_x;
    float modifier = 1.0f;

    float nutrient = world->nutrients ? world->nutrients[target_idx] : 0.5f;
    float toxin = world->toxins ? world->toxins[target_idx] : 0.0f;
    modifier *= (1.0f + colony->genome.nutrient_sensitivity * (nutrient - 0.45f) * 0.8f);
    modifier *= (1.0f - colony->genome.toxin_sensitivity * toxin * 0.9f);

    if (world->signals && world->signal_source && world->signal_source[target_idx] != 0) {
        float signal = world->signals[target_idx];
        if (world->signal_source[target_idx] == colony_id) {
            modifier *= (1.0f + colony->genome.signal_sensitivity * signal * 0.25f);
        } else {
            modifier *= (1.0f - colony->genome.signal_sensitivity * signal * 0.12f * (1.0f - colony->genome.merge_affinity));
        }
    }

    if (world->alarm_signals && world->alarm_source && world->alarm_signals[target_idx] > 0.001f) {
        float alarm = world->alarm_signals[target_idx];
        if (world->alarm_source[target_idx] == colony_id) {
            float reinforcement = colony->genome.aggression - colony->genome.defense_priority;
            modifier *= (1.0f + alarm * reinforcement * 0.20f);
            modifier *= (1.0f - alarm * fmaxf(0.0f, colony->genome.defense_priority - colony->genome.aggression) * 0.25f);
        } else {
            modifier *= (1.0f - alarm * (0.18f + colony->genome.signal_sensitivity * 0.22f));
        }
    }

    if (source_total > 0) {
        float local_density = (float)source_friendly / (float)source_total;
        if (local_density > colony->genome.quorum_threshold) {
            float density_penalty = (local_density - colony->genome.quorum_threshold) * (1.0f - colony->genome.density_tolerance);
            modifier *= (1.0f - density_penalty);
        }
    }

    if (colony->is_dormant || colony->state == COLONY_STATE_DORMANT) {
        modifier *= 0.2f + nutrient * (0.45f + colony->genome.dormancy_resistance * 0.2f);
    } else if (colony->state == COLONY_STATE_STRESSED) {
        modifier *= (0.85f + colony->genome.aggression * 0.25f);
    }

    modifier *= (0.75f + colony->behavior_actions[COLONY_ACTION_EXPAND] * 0.60f);
    modifier *= (1.0f - colony->behavior_actions[COLONY_ACTION_DORMANCY] * 0.45f);

    modifier *= (1.0f - colony->biofilm_strength * colony->genome.biofilm_investment * 0.12f);
    modifier *= (0.9f + colony->success_history[dir_index] * (0.15f + colony->genome.learning_rate * 0.15f));

    float drift_alignment = atomic_direction_alignment(target_x - source_x, target_y - source_y, colony->drift_x, colony->drift_y);
    modifier *= (1.0f + drift_alignment * colony->genome.motility * (0.2f + colony->behavior_actions[COLONY_ACTION_MOTILITY] * 0.35f));

    if (source_enemy > 0) {
        float frontier_bias = colony->genome.specialization * (colony->genome.aggression + colony->genome.defense_priority * 0.5f - 0.5f);
        modifier *= (1.0f + frontier_bias * 0.3f);
        modifier *= (0.9f + colony->behavior_actions[COLONY_ACTION_ATTACK] * 0.55f);
        modifier *= (0.95f + colony->behavior_actions[COLONY_ACTION_DEFEND] * 0.20f);
    } else if (source_empty == 0 && source_total > 0 && source_friendly * 2 >= source_total) {
        modifier *= (1.0f - colony->genome.specialization * 0.1f);
        modifier *= (0.92f + colony->behavior_actions[COLONY_ACTION_SIGNAL] * 0.18f);
    }

    return utils_clamp_f(modifier, 0.15f, 2.5f);
}

static void atomic_rebuild_spread_frontier(AtomicWorld* aworld) {
    if (!aworld || !aworld->spread_frontier_indices) {
        return;
    }

    int width = aworld->grid.width;
    int height = aworld->grid.height;
    int cell_count = width * height;
    if (cell_count <= 0) {
        aworld->spread_state.spread_frontier_count = 0;
        return;
    }

    AtomicCell* current = grid_current(&aworld->grid);

    uint32_t tick = aworld->world ? (uint32_t)aworld->world->tick : 0u;
    bool reverse_y = (tick & 1u) != 0u;
    bool reverse_x_base = ((tick >> 1) & 1u) != 0u;

    int frontier_count = 0;
    for (int row = 0; row < height; row++) {
        int y = reverse_y ? (height - 1 - row) : row;
        int row_base = y * width;
        bool reverse_x = reverse_x_base ^ ((row & 1) != 0);

        if (reverse_x) {
            for (int x = width - 1; x >= 0; x--) {
                int idx = row_base + x;
                uint32_t colony_id = atomic_load_explicit(&current[idx].colony_id, memory_order_relaxed);
                if (colony_id == 0) {
                    continue;
                }

                if (!atomic_cell_has_empty_neighbor(current, width, height, idx)) {
                    continue;
                }

                aworld->spread_frontier_indices[frontier_count++] = idx;
            }
        } else {
            for (int x = 0; x < width; x++) {
                int idx = row_base + x;
                uint32_t colony_id = atomic_load_explicit(&current[idx].colony_id, memory_order_relaxed);
                if (colony_id == 0) {
                    continue;
                }

                if (!atomic_cell_has_empty_neighbor(current, width, height, idx)) {
                    continue;
                }

                aworld->spread_frontier_indices[frontier_count++] = idx;
            }
        }
    }

    aworld->spread_state.spread_frontier_count = frontier_count;
}

static void atomic_prepare_next_buffer(AtomicWorld* aworld) {
    if (!aworld) {
        return;
    }

    DoubleBufferedGrid* grid = &aworld->grid;
    AtomicCell* current = grid_current(grid);
    AtomicCell* next = grid_next(grid);
    int cell_count = grid->width * grid->height;

    for (int i = 0; i < cell_count; i++) {
        uint32_t colony_id = atomic_load_explicit(&current[i].colony_id, memory_order_relaxed);
        uint8_t age = atomic_load_explicit(&current[i].age, memory_order_relaxed);

        atomic_store_explicit(&next[i].colony_id, colony_id, memory_order_relaxed);
        atomic_store_explicit(&next[i].age, age, memory_order_relaxed);
        next[i].is_border = current[i].is_border;
    }
}

static inline void atomic_record_spread_delta(
    AtomicWorld* aworld,
    int32_t* slot_deltas,
    uint32_t* slot_touched,
    uint32_t* touched_count,
    uint32_t colony_id
) {
    if (colony_id >= aworld->max_colonies) {
        return;
    }

    if (slot_deltas[colony_id] == 0 && *touched_count < aworld->max_colonies) {
        slot_touched[*touched_count] = colony_id;
        (*touched_count)++;
    }
    slot_deltas[colony_id]++;
}

static void atomic_spread_from_cell(
    AtomicWorld* aworld,
    int x,
    int y,
    int32_t* slot_deltas,
    uint32_t* slot_touched,
    uint32_t* touched_count,
    uint32_t* rng_state
) {
    World* world = aworld->world;
    DoubleBufferedGrid* grid = &aworld->grid;
    int width = grid->width;
    int height = grid->height;
    AtomicCell* current = grid_current(grid);
    AtomicCell* next = grid_next(grid);
    AtomicCell* cell = &current[y * width + x];

    uint32_t colony_id = atomic_load_explicit(&cell->colony_id, memory_order_relaxed);
    if (colony_id == 0 || colony_id >= aworld->max_colonies) {
        return;
    }

    uint8_t age = atomic_load_explicit(&cell->age, memory_order_relaxed);
    if (age == 0) {
        return;
    }

    Colony* colony = NULL;
    if ((size_t)colony_id < world->colony_index_capacity) {
        uint32_t idx = world->colony_index_map[colony_id];
        if (idx != UINT32_MAX && idx < world->colony_count) {
            Colony* candidate = &world->colonies[idx];
            if (candidate->id == colony_id && candidate->active) {
                colony = candidate;
            }
        }
    }
    if (!colony) {
        return;
    }

    float social_neighbor_dx = 0.0f;
    float social_neighbor_dy = 0.0f;
    bool has_social_neighbor = calculate_social_vector(
        world,
        grid,
        x,
        y,
        colony,
        &social_neighbor_dx,
        &social_neighbor_dy
    );
    float social_factor = colony->genome.social_factor;
    int source_friendly = 0;
    int source_enemy = 0;
    int source_empty = 0;
    int source_total = 0;
    atomic_count_neighbor_mix(current, width, height, x, y, colony_id,
                              &source_friendly, &source_enemy, &source_empty, &source_total);

    for (int d = 0; d < 8; d++) {
        int dx = DX8[d];
        int dy = DY8[d];
        int nx = x + dx;
        int ny = y + dy;

        if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
            continue;
        }

        AtomicCell* neighbor = &current[ny * width + nx];

        uint32_t neighbor_colony = atomic_load_explicit(&neighbor->colony_id, memory_order_relaxed);
        float spread_prob = colony->genome.spread_rate * colony->genome.metabolism * colony->genome.spread_weights[d];
        spread_prob *= atomic_calculate_behavioral_spread_modifier(
            aworld,
            colony,
            colony_id,
            x,
            y,
            nx,
            ny,
            d,
            source_friendly,
            source_enemy,
            source_empty,
            source_total
        );

        if (has_social_neighbor) {
            spread_prob *= calculate_social_influence_for_direction(
                dx,
                dy,
                social_factor,
                social_neighbor_dx,
                social_neighbor_dy
            );
        }

        if (neighbor_colony == 0) {
            if (rand_float_local(rng_state) < spread_prob) {
                AtomicCell* next_neighbor = &next[ny * width + nx];
                if (atomic_cell_try_claim(next_neighbor, 0, colony_id)) {
                    atomic_store_explicit(&next_neighbor->age, 0, memory_order_relaxed);
                    next_neighbor->is_border = 0;
                    atomic_record_spread_delta(aworld, slot_deltas, slot_touched, touched_count, colony_id);
                }
            }
        } else if (neighbor_colony != colony_id) {
            continue;
        }
    }
}

static void* atomic_phase_worker(void* arg) {
    AtomicRegionWork* worker = (AtomicRegionWork*)arg;
    AtomicWorld* aworld = worker->aworld;
    int worker_id = worker->thread_id;
    if (!aworld || worker_id < 0 || worker_id >= aworld->phase_worker_count) {
        return NULL;
    }
    uint32_t seen_generation = 0;

    while (1) {
        pthread_mutex_lock(&aworld->phase_mutex);
        while (!aworld->phase_state.phase_shutdown && seen_generation == aworld->phase_state.phase_generation) {
            pthread_cond_wait(&aworld->phase_cond, &aworld->phase_mutex);
        }

        if (aworld->phase_state.phase_shutdown) {
            pthread_mutex_unlock(&aworld->phase_mutex);
            break;
        }

        seen_generation = aworld->phase_state.phase_generation;
        int phase = aworld->phase_state.active_phase;
        int start = aworld->worker_region_start[worker_id];
        int end = aworld->worker_region_end[worker_id];
        int stride = aworld->phase_state.phase_region_stride;
        if (stride < 1) {
            stride = 1;
        }
        pthread_mutex_unlock(&aworld->phase_mutex);

        if (phase == ATOMIC_PHASE_AGE) {
            for (int i = start; i < end; i += stride) {
                atomic_age_region(&aworld->region_work[i]);
            }
        } else if (phase == ATOMIC_PHASE_SPREAD) {
            for (int i = start; i < end; i += stride) {
                atomic_spread_region(&aworld->region_work[i]);
            }
        } else if (phase == ATOMIC_PHASE_SPREAD_FRONTIER) {
            size_t slot_base = (size_t)worker_id * aworld->max_colonies;
            int32_t* slot_deltas = &aworld->spread_deltas[slot_base];
            uint32_t* slot_touched = &aworld->spread_touched_ids[slot_base];
            uint32_t touched_count = 0;
            uint32_t rng_state = aworld->thread_seeds[worker_id];
            bool reverse_frontier = (((aworld->world ? (uint32_t)aworld->world->tick : 0u) + (uint32_t)worker_id) & 1u) != 0u;

            if (reverse_frontier) {
                int last = end - 1;
                if (stride > 1 && last >= start) {
                    last -= (last - start) % stride;
                }
                for (int i = last; i >= start; i -= stride) {
                    int idx = aworld->spread_frontier_indices[i];
                    int x = idx % aworld->grid.width;
                    int y = idx / aworld->grid.width;
                    atomic_spread_from_cell(
                        aworld,
                        x,
                        y,
                        slot_deltas,
                        slot_touched,
                        &touched_count,
                        &rng_state
                    );
                }
            } else {
                for (int i = start; i < end; i += stride) {
                    int idx = aworld->spread_frontier_indices[i];
                    int x = idx % aworld->grid.width;
                    int y = idx / aworld->grid.width;
                    atomic_spread_from_cell(
                        aworld,
                        x,
                        y,
                        slot_deltas,
                        slot_touched,
                        &touched_count,
                        &rng_state
                    );
                }
            }

            aworld->spread_touched_counts[worker_id] = touched_count;
            aworld->thread_seeds[worker_id] = rng_state;
        }

        pthread_mutex_lock(&aworld->phase_mutex);
        aworld->phase_state.phase_done_count++;
        if (aworld->phase_state.phase_done_count >= aworld->phase_worker_count) {
            pthread_cond_signal(&aworld->phase_done_cond);
        }
        pthread_mutex_unlock(&aworld->phase_mutex);
    }

    return NULL;
}

static int atomic_phase_system_create(AtomicWorld* aworld) {
    if (!aworld || aworld->thread_count <= 0 || aworld->region_count <= 0) {
        return -1;
    }

    aworld->phase_worker_count = aworld->thread_count;
    if (aworld->phase_worker_count > aworld->region_count) {
        aworld->phase_worker_count = aworld->region_count;
    }
    if (aworld->phase_worker_count < 1) {
        return -1;
    }

    aworld->phase_threads = (pthread_t*)calloc((size_t)aworld->phase_worker_count, sizeof(pthread_t));
    aworld->phase_worker_args = (AtomicRegionWork*)calloc((size_t)aworld->phase_worker_count, sizeof(AtomicRegionWork));
    aworld->worker_region_start = (int*)calloc((size_t)aworld->phase_worker_count, sizeof(int));
    aworld->worker_region_end = (int*)calloc((size_t)aworld->phase_worker_count, sizeof(int));
    if (!aworld->phase_threads || !aworld->phase_worker_args || !aworld->worker_region_start || !aworld->worker_region_end) {
        free(aworld->phase_threads);
        free(aworld->phase_worker_args);
        free(aworld->worker_region_start);
        free(aworld->worker_region_end);
        aworld->phase_threads = NULL;
        aworld->phase_worker_args = NULL;
        aworld->worker_region_start = NULL;
        aworld->worker_region_end = NULL;
        return -1;
    }

    if (pthread_mutex_init(&aworld->phase_mutex, NULL) != 0) {
        free(aworld->phase_threads);
        free(aworld->worker_region_start);
        free(aworld->worker_region_end);
        aworld->phase_threads = NULL;
        aworld->worker_region_start = NULL;
        aworld->worker_region_end = NULL;
        return -1;
    }
    if (pthread_cond_init(&aworld->phase_cond, NULL) != 0) {
        pthread_mutex_destroy(&aworld->phase_mutex);
        free(aworld->phase_threads);
        free(aworld->phase_worker_args);
        free(aworld->worker_region_start);
        free(aworld->worker_region_end);
        aworld->phase_threads = NULL;
        aworld->phase_worker_args = NULL;
        aworld->worker_region_start = NULL;
        aworld->worker_region_end = NULL;
        return -1;
    }
    if (pthread_cond_init(&aworld->phase_done_cond, NULL) != 0) {
        pthread_cond_destroy(&aworld->phase_cond);
        pthread_mutex_destroy(&aworld->phase_mutex);
        free(aworld->phase_threads);
        free(aworld->phase_worker_args);
        free(aworld->worker_region_start);
        free(aworld->worker_region_end);
        aworld->phase_threads = NULL;
        aworld->phase_worker_args = NULL;
        aworld->worker_region_start = NULL;
        aworld->worker_region_end = NULL;
        return -1;
    }

    for (int w = 0; w < aworld->phase_worker_count; w++) {
        int start = (w * aworld->region_count) / aworld->phase_worker_count;
        int end = ((w + 1) * aworld->region_count) / aworld->phase_worker_count;
        aworld->worker_region_start[w] = start;
        aworld->worker_region_end[w] = end;
    }

    aworld->phase_state.phase_generation = 0;
    aworld->phase_state.phase_done_count = 0;
    aworld->phase_state.active_phase = ATOMIC_PHASE_IDLE;
    aworld->phase_state.phase_region_stride = 1;
    aworld->phase_state.phase_shutdown = false;

    for (int w = 0; w < aworld->phase_worker_count; w++) {
        aworld->phase_worker_args[w].aworld = aworld;
        aworld->phase_worker_args[w].thread_id = w;
        if (pthread_create(&aworld->phase_threads[w], NULL, atomic_phase_worker, &aworld->phase_worker_args[w]) != 0) {
            aworld->phase_state.phase_shutdown = true;
            pthread_cond_broadcast(&aworld->phase_cond);
            for (int j = 0; j < w; j++) {
                pthread_join(aworld->phase_threads[j], NULL);
            }
            pthread_cond_destroy(&aworld->phase_done_cond);
            pthread_cond_destroy(&aworld->phase_cond);
            pthread_mutex_destroy(&aworld->phase_mutex);
            free(aworld->phase_threads);
            free(aworld->phase_worker_args);
            free(aworld->worker_region_start);
            free(aworld->worker_region_end);
            aworld->phase_threads = NULL;
            aworld->phase_worker_args = NULL;
            aworld->worker_region_start = NULL;
            aworld->worker_region_end = NULL;
            return -1;
        }
    }

    aworld->phase_state.phase_system_ready = true;
    return 0;
}

static void atomic_phase_system_destroy(AtomicWorld* aworld) {
    if (!aworld || !aworld->phase_state.phase_system_ready) {
        return;
    }

    pthread_mutex_lock(&aworld->phase_mutex);
    aworld->phase_state.phase_shutdown = true;
    pthread_cond_broadcast(&aworld->phase_cond);
    pthread_mutex_unlock(&aworld->phase_mutex);

    for (int w = 0; w < aworld->phase_worker_count; w++) {
        pthread_join(aworld->phase_threads[w], NULL);
    }

    pthread_cond_destroy(&aworld->phase_done_cond);
    pthread_cond_destroy(&aworld->phase_cond);
    pthread_mutex_destroy(&aworld->phase_mutex);
    aworld->phase_state.phase_system_ready = false;
}

static void atomic_run_phase(AtomicWorld* aworld, int phase) {
    if (!aworld || !aworld->phase_state.phase_system_ready) {
        return;
    }

    pthread_mutex_lock(&aworld->phase_mutex);
    aworld->phase_state.active_phase = phase;
    aworld->phase_state.phase_done_count = 0;
    aworld->phase_state.phase_generation++;
    pthread_cond_broadcast(&aworld->phase_cond);

    while (aworld->phase_state.phase_done_count < aworld->phase_worker_count) {
        pthread_cond_wait(&aworld->phase_done_cond, &aworld->phase_mutex);
    }

    aworld->phase_state.active_phase = ATOMIC_PHASE_IDLE;
    pthread_mutex_unlock(&aworld->phase_mutex);
}

static void atomic_phase_assign_ranges(AtomicWorld* aworld, int total_units) {
    if (!aworld || !aworld->phase_state.phase_system_ready || total_units < 0) {
        return;
    }

    pthread_mutex_lock(&aworld->phase_mutex);
    aworld->phase_state.phase_region_stride = 1;
    for (int w = 0; w < aworld->phase_worker_count; w++) {
        int start = (w * total_units) / aworld->phase_worker_count;
        int end = ((w + 1) * total_units) / aworld->phase_worker_count;
        aworld->worker_region_start[w] = start;
        aworld->worker_region_end[w] = end;
    }
    pthread_mutex_unlock(&aworld->phase_mutex);
}

static void atomic_phase_assign_interleaved(AtomicWorld* aworld, int total_units) {
    if (!aworld || !aworld->phase_state.phase_system_ready || total_units < 0) {
        return;
    }

    pthread_mutex_lock(&aworld->phase_mutex);
    aworld->phase_state.phase_region_stride = aworld->phase_worker_count;
    if (aworld->phase_state.phase_region_stride < 1) {
        aworld->phase_state.phase_region_stride = 1;
    }

    for (int w = 0; w < aworld->phase_worker_count; w++) {
        aworld->worker_region_start[w] = w;
        aworld->worker_region_end[w] = total_units;
    }
    pthread_mutex_unlock(&aworld->phase_mutex);
}

// ============================================================================
// Social/Chemotaxis behavior - neighbor detection and influence
// ============================================================================

/**
 * Calculate normalized direction toward nearest detected neighbor colony.
 * 
 * Detects nearby colonies and adjusts spread probability:
 * - Positive social_factor: bias toward nearest neighbors (attracted)
 * - Negative social_factor: bias away from nearest neighbors (repelled)
 * 
 * Returns true when a usable neighbor direction is available.
 */
static bool calculate_social_vector(
    World* world,
    DoubleBufferedGrid* grid,
    int cell_x, int cell_y,
    Colony* colony,
    float* out_norm_dx,
    float* out_norm_dy
) {
    const Genome* genome = &colony->genome;
    const int width = world->width;
    const int height = world->height;
    AtomicCell* cells = grid_current(grid);
    
    // Skip if no social behavior
    if (fabsf(genome->social_factor) < 0.01f) {
        return false;
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
    
    int min_y = cell_y - detect_radius;
    if (min_y < 0) min_y = 0;
    int max_y = cell_y + detect_radius;
    if (max_y >= height) max_y = height - 1;

    int min_x = cell_x - detect_radius;
    if (min_x < 0) min_x = 0;
    int max_x = cell_x + detect_radius;
    if (max_x >= width) max_x = width - 1;

    for (int check_y = min_y; check_y <= max_y && found < genome->max_tracked; check_y += step) {
        int dy = check_y - cell_y;
        int row_base = check_y * width;

        for (int check_x = min_x; check_x <= max_x && found < genome->max_tracked; check_x += step) {
            int dx = check_x - cell_x;
            if (dx == 0 && dy == 0) continue;

            AtomicCell* check_cell = &cells[row_base + check_x];
            uint32_t check_colony_id = atomic_load_explicit(&check_cell->colony_id, memory_order_relaxed);
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
        return false;
    }
    
    // Normalize direction to nearest colony
    float dist = sqrtf(nearest_dist_sq);
    if (dist < 1.0f) dist = 1.0f;
    *out_norm_dx = nearest_dx / dist;
    *out_norm_dy = nearest_dy / dist;

    return true;
}

static float calculate_social_influence_for_direction(
    int spread_dx,
    int spread_dy,
    float social_factor,
    float neighbor_norm_dx,
    float neighbor_norm_dy
) {
    // Normalize spread direction
    float spread_len = sqrtf((float)(spread_dx * spread_dx + spread_dy * spread_dy));
    if (spread_len < 0.1f) {
        return 1.0f;
    }
    float spread_norm_dx = (float)spread_dx / spread_len;
    float spread_norm_dy = (float)spread_dy / spread_len;
    
    // Dot product: positive if spreading toward neighbor, negative if away
    float dot = spread_norm_dx * neighbor_norm_dx + spread_norm_dy * neighbor_norm_dy;
    
    // Apply social factor:
    // Positive social_factor + positive dot = boost (attracted, moving toward)
    // Negative social_factor + positive dot = reduce (repelled, want to move away)
    float influence = 1.0f + social_factor * dot * 0.4f;
    
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
    aworld->serial_interval = atomic_parse_env_int("FEROX_ATOMIC_SERIAL_INTERVAL", 5, 1, 32);
    aworld->frontier_dense_pct = atomic_parse_env_int("FEROX_ATOMIC_FRONTIER_DENSE_PCT", 15, 5, 90);
    
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
    
    int regions_x = 1;
    int regions_y = 1;
    compute_region_grid(aworld->grid.width, aworld->grid.height, aworld->thread_count, &regions_x, &regions_y);
    aworld->region_count = regions_x * regions_y;

    int seed_count = aworld->region_count;
    if (seed_count < aworld->thread_count) {
        seed_count = aworld->thread_count;
    }
    aworld->thread_seeds = (uint32_t*)malloc((size_t)seed_count * sizeof(uint32_t));
    if (!aworld->thread_seeds) {
        free(aworld->colony_stats);
        free(aworld->grid.buffers[0]);
        free(aworld->grid.buffers[1]);
        free(aworld);
        return NULL;
    }

    for (int i = 0; i < seed_count; i++) {
        aworld->thread_seeds[i] = (uint32_t)(12345 + i * 7919);
    }

    aworld->region_work = (AtomicRegionWork*)calloc((size_t)aworld->region_count, sizeof(AtomicRegionWork));
    if (!aworld->region_work) {
        free(aworld->thread_seeds);
        free(aworld->colony_stats);
        free(aworld->grid.buffers[0]);
        free(aworld->grid.buffers[1]);
        free(aworld);
        return NULL;
    }

    aworld->submit_args = (void**)calloc((size_t)aworld->region_count, sizeof(void*));
    if (!aworld->submit_args) {
        free(aworld->region_work);
        free(aworld->thread_seeds);
        free(aworld->colony_stats);
        free(aworld->grid.buffers[0]);
        free(aworld->grid.buffers[1]);
        free(aworld);
        return NULL;
    }

    if (atomic_alloc_spread_tracking(aworld, aworld->max_colonies) != 0) {
        free(aworld->submit_args);
        free(aworld->region_work);
        free(aworld->thread_seeds);
        free(aworld->colony_stats);
        free(aworld->grid.buffers[0]);
        free(aworld->grid.buffers[1]);
        free(aworld);
        return NULL;
    }

    int cell_count = aworld->grid.width * aworld->grid.height;
    aworld->spread_frontier_indices = (int*)malloc((size_t)cell_count * sizeof(int));
    if (!aworld->spread_frontier_indices) {
        free(aworld->spread_frontier_indices);
        free(aworld->spread_touched_counts);
        free(aworld->spread_touched_ids);
        free(aworld->spread_deltas);
        free(aworld->submit_args);
        free(aworld->region_work);
        free(aworld->thread_seeds);
        free(aworld->colony_stats);
        free(aworld->grid.buffers[0]);
        free(aworld->grid.buffers[1]);
        free(aworld);
        return NULL;
    }
    aworld->spread_state.spread_frontier_enabled = atomic_parse_env_bool("FEROX_ATOMIC_USE_FRONTIER", true);
    aworld->spread_state.spread_frontier_count = 0;

    int region_width = aworld->grid.width / regions_x;
    int region_height = aworld->grid.height / regions_y;
    for (int ry = 0; ry < regions_y; ry++) {
        for (int rx = 0; rx < regions_x; rx++) {
            int idx = ry * regions_x + rx;
            AtomicRegionWork* work = &aworld->region_work[idx];
            work->aworld = aworld;
            work->region_index = idx;
            work->start_x = rx * region_width;
            work->start_y = ry * region_height;
            work->end_x = (rx == regions_x - 1) ? aworld->grid.width : (rx + 1) * region_width;
            work->end_y = (ry == regions_y - 1) ? aworld->grid.height : (ry + 1) * region_height;
            work->thread_id = idx;
        }
    }

    if (atomic_phase_system_create(aworld) != 0) {
        aworld->phase_state.phase_system_ready = false;
    }
    
    // Sync from world
    atomic_world_sync_from_world(aworld);
    
    return aworld;
}

void atomic_world_destroy(AtomicWorld* aworld) {
    if (!aworld) return;
    
    atomic_phase_system_destroy(aworld);

    free(aworld->worker_region_end);
    free(aworld->worker_region_start);
    free(aworld->phase_worker_args);
    free(aworld->phase_threads);
    free(aworld->spread_frontier_indices);
    free(aworld->spread_deltas);
    free(aworld->spread_touched_ids);
    free(aworld->spread_touched_counts);
    free(aworld->submit_args);
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

            if (atomic_alloc_spread_tracking(aworld, aworld->max_colonies) != 0) {
                fprintf(stderr, "Warning: Failed to resize spread tracking buffers (max=%zu)\n", aworld->max_colonies);
            }
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
        atomic_store_explicit(&aworld->colony_stats[i].cell_count, 0, memory_order_relaxed);
    }
    
    // Copy cell data and count populations
    for (int i = 0; i < world->width * world->height; i++) {
        Cell* cell = &world->cells[i];
        
        atomic_store_explicit(&current[i].colony_id, cell->colony_id, memory_order_relaxed);
        atomic_store_explicit(&current[i].age, cell->age, memory_order_relaxed);
        current[i].is_border = cell->is_border ? 1 : 0;
        
        // Also init next buffer
        atomic_store_explicit(&next[i].colony_id, cell->colony_id, memory_order_relaxed);
        atomic_store_explicit(&next[i].age, cell->age, memory_order_relaxed);
        next[i].is_border = cell->is_border ? 1 : 0;
        
        // Count population
        if (cell->colony_id != 0 && cell->colony_id < aworld->max_colonies) {
            atomic_fetch_add_explicit(&aworld->colony_stats[cell->colony_id].cell_count, 1, memory_order_relaxed);
        }
    }
    
    // Update max counts from world (only increase, never decrease)
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (colony->id < aworld->max_colonies) {
            int64_t current_max = atomic_load_explicit(&aworld->colony_stats[colony->id].max_cell_count, memory_order_relaxed);
            int64_t world_max = (int64_t)colony->max_cell_count;
            if (world_max > current_max) {
                atomic_store_explicit(&aworld->colony_stats[colony->id].max_cell_count, world_max, memory_order_relaxed);
            }
        }
    }

    atomic_rebuild_spread_frontier(aworld);
}

void atomic_world_sync_to_world(AtomicWorld* aworld) {
    if (!aworld || !aworld->world) return;
    
    World* world = aworld->world;
    AtomicCell* current = grid_current(&aworld->grid);
    
    // Copy cell data back
    for (int i = 0; i < world->width * world->height; i++) {
        world->cells[i].colony_id = atomic_load_explicit(&current[i].colony_id, memory_order_relaxed);
        world->cells[i].age = atomic_load_explicit(&current[i].age, memory_order_relaxed);
        world->cells[i].is_border = current[i].is_border != 0;
    }
    
    // Update colony stats
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (colony->id < aworld->max_colonies) {
            colony->cell_count = (size_t)atomic_load_explicit(&aworld->colony_stats[colony->id].cell_count, memory_order_relaxed);
            
            int64_t max = atomic_load_explicit(&aworld->colony_stats[colony->id].max_cell_count, memory_order_relaxed);
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
    size_t region_base = (size_t)work->region_index * aworld->max_colonies;
    int32_t* region_deltas = &aworld->spread_deltas[region_base];
    uint32_t* region_touched = &aworld->spread_touched_ids[region_base];
    uint32_t touched_count = aworld->spread_touched_counts[work->region_index];
    
    // Thread-local RNG
    uint32_t rng_state = aworld->thread_seeds[work->thread_id];

    uint32_t tick = aworld->world ? (uint32_t)aworld->world->tick : 0u;
    bool reverse_y = ((tick + (uint32_t)work->region_index) & 1u) != 0u;
    bool reverse_x_base = (((tick >> 1) + (uint32_t)work->region_index) & 1u) != 0u;
    int row_count = work->end_y - work->start_y;

    for (int row = 0; row < row_count; row++) {
        int y = reverse_y ? (work->end_y - 1 - row) : (work->start_y + row);
        bool reverse_x = reverse_x_base ^ ((row & 1) != 0);

        if (reverse_x) {
            for (int x = work->end_x - 1; x >= work->start_x; x--) {
                atomic_spread_from_cell(
                    aworld,
                    x,
                    y,
                    region_deltas,
                    region_touched,
                    &touched_count,
                    &rng_state
                );
            }
        } else {
            for (int x = work->start_x; x < work->end_x; x++) {
                atomic_spread_from_cell(
                    aworld,
                    x,
                    y,
                    region_deltas,
                    region_touched,
                    &touched_count,
                    &rng_state
                );
            }
        }
    }

    aworld->spread_touched_counts[work->region_index] = touched_count;
    
    // Save RNG state back (for reproducibility)
    aworld->thread_seeds[work->thread_id] = rng_state;
}

void atomic_age_region(AtomicRegionWork* work) {
    DoubleBufferedGrid* grid = &work->aworld->grid;
    int width = grid->width;
    AtomicCell* cells = grid_current(grid);

    for (int y = work->start_y; y < work->end_y; y++) {
        int row = y * width;
        for (int x = work->start_x; x < work->end_x; x++) {
            AtomicCell* cell = &cells[row + x];
            if (atomic_load_explicit(&cell->colony_id, memory_order_relaxed) != 0) {
                atomic_cell_age(cell);
            }
        }
    }
}

// ============================================================================
// Parallel Phases
// ============================================================================

static void submit_region_tasks(AtomicWorld* aworld, void (*task_func)(void*)) {
    if (!aworld || !aworld->region_work || aworld->region_count <= 0) {
        return;
    }

    for (int i = 0; i < aworld->region_count; i++) {
        aworld->submit_args[i] = &aworld->region_work[i];
    }

    threadpool_submit_batch(aworld->pool, task_func, aworld->submit_args, aworld->region_count);
}

void atomic_age(AtomicWorld* aworld) {
    if (!aworld) return;
    if (aworld->phase_state.phase_system_ready) {
        atomic_phase_assign_ranges(aworld, aworld->region_count);
        atomic_run_phase(aworld, ATOMIC_PHASE_AGE);
        return;
    }

    submit_region_tasks(aworld, age_task_func);
}

void atomic_spread(AtomicWorld* aworld) {
    if (!aworld) return;

    aworld->spread_state.spread_slots_used = aworld->region_count;

    if (aworld->phase_state.phase_system_ready) {
        bool use_frontier = aworld->spread_state.spread_frontier_enabled && aworld->spread_state.spread_frontier_count > 0;
        int total_cells = aworld->grid.width * aworld->grid.height;
        if (use_frontier && total_cells > 0) {
            int dense_cutoff = (total_cells * aworld->frontier_dense_pct) / 100;
            if (dense_cutoff < 1) {
                dense_cutoff = 1;
            }
            if (aworld->spread_state.spread_frontier_count >= dense_cutoff) {
                use_frontier = false;
            }
        }

        if (use_frontier) {
            aworld->spread_state.spread_slots_used = aworld->phase_worker_count;
            atomic_phase_assign_interleaved(aworld, aworld->spread_state.spread_frontier_count);
            atomic_run_phase(aworld, ATOMIC_PHASE_SPREAD_FRONTIER);
        } else {
            aworld->spread_state.spread_slots_used = aworld->region_count;
            atomic_phase_assign_interleaved(aworld, aworld->region_count);
            atomic_run_phase(aworld, ATOMIC_PHASE_SPREAD);
        }
        return;
    }

    submit_region_tasks(aworld, spread_task_func);
}

void atomic_spread_step(AtomicWorld* aworld) {
    if (!aworld) return;

    atomic_prepare_next_buffer(aworld);
    atomic_spread(aworld);
    atomic_barrier(aworld);
    atomic_spread_apply_deltas(aworld);
    grid_swap(&aworld->grid);
    atomic_rebuild_spread_frontier(aworld);
}

int64_t atomic_spread_apply_deltas(AtomicWorld* aworld) {
    return atomic_apply_spread_deltas_internal(aworld);
}

void atomic_set_spread_frontier_enabled(AtomicWorld* aworld, bool enabled) {
    if (!aworld) {
        return;
    }
    aworld->spread_state.spread_frontier_enabled = enabled;
}

int atomic_get_spread_frontier_count(AtomicWorld* aworld) {
    if (!aworld) {
        return 0;
    }
    return aworld->spread_state.spread_frontier_count;
}

void atomic_barrier(AtomicWorld* aworld) {
    if (!aworld) return;
    if (aworld->phase_state.phase_system_ready) {
        return;
    }
    threadpool_wait(aworld->pool);
}

// ============================================================================
// Main Tick Function
// ============================================================================

void atomic_tick(AtomicWorld* aworld) {
    if (!aworld || !aworld->world) return;
    
    World* world = aworld->world;

    // Refresh environmental pressure, toxins, and signaling layers based on the
    // current world state before the parallel spread phase reads them.
    simulation_update_behavior_layers(world);

    // === Parallel Phase ===
    
    // Age all cells in parallel
    atomic_age(aworld);
    atomic_barrier(aworld);
    
    // Spread colonies in parallel using atomic CAS
    atomic_spread_step(aworld);
    
    // Sync atomic state back to regular world for output and/or serial maintenance.
    atomic_world_sync_to_world(aworld);

    bool run_serial = (aworld->serial_interval <= 1) || ((int)(world->tick % (uint64_t)aworld->serial_interval) == 0);
    if (run_serial) {
        // Mutations (per-colony, serial)
        simulation_mutate(world);

        // Division detection (requires flood-fill, serial)
        simulation_check_divisions(world);

        // Recombination (serial)
        simulation_check_recombinations(world);

        // Strategic combat and toxin warfare on the active runtime path.
        simulation_resolve_combat(world);

        // Recount after structural and combat changes.
        simulation_recount_colony_cells(world);

        // Contact-based adaptation between neighboring colonies.
        simulation_apply_horizontal_gene_transfer(world);

        // Keep colony state, biofilm, and movement dynamics current.
        simulation_update_colony_dynamics(world);

        // Sync structural changes back to atomic world.
        atomic_world_sync_from_world(aworld);
    } else {
        // Keep colony state and behavior moving on non-maintenance ticks.
        simulation_update_colony_dynamics(world);
    }

    world->tick++;
}

void atomic_tick_with_breakdown(AtomicWorld* aworld, AtomicTickBreakdown* breakdown) {
    if (!aworld || !aworld->world) {
        if (breakdown) {
            memset(breakdown, 0, sizeof(*breakdown));
        }
        return;
    }

    AtomicTickBreakdown local = {0};
    World* world = aworld->world;
    double total_start = atomic_now_ms();

    double phase_start = atomic_now_ms();
    simulation_update_behavior_layers(world);
    atomic_age(aworld);
    atomic_barrier(aworld);
    local.age_ms = atomic_now_ms() - phase_start;

    phase_start = atomic_now_ms();
    atomic_spread_step(aworld);
    local.spread_ms = atomic_now_ms() - phase_start;

    phase_start = atomic_now_ms();
    atomic_world_sync_to_world(aworld);
    local.sync_to_world_ms = atomic_now_ms() - phase_start;

    bool run_serial = (aworld->serial_interval <= 1) ||
                      ((int)(world->tick % (uint64_t)aworld->serial_interval) == 0);
    phase_start = atomic_now_ms();
    if (run_serial) {
        simulation_mutate(world);
        simulation_check_divisions(world);
        simulation_check_recombinations(world);
        simulation_resolve_combat(world);
        simulation_recount_colony_cells(world);
        simulation_apply_horizontal_gene_transfer(world);
        simulation_update_colony_dynamics(world);
    } else {
        simulation_update_colony_dynamics(world);
    }
    local.serial_ms = atomic_now_ms() - phase_start;

    phase_start = atomic_now_ms();
    if (run_serial) {
        atomic_world_sync_from_world(aworld);
    }
    local.sync_from_world_ms = atomic_now_ms() - phase_start;

    world->tick++;
    local.total_ms = atomic_now_ms() - total_start;

    if (breakdown) {
        *breakdown = local;
    }
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
