#include "simulation.h"
#include "genetics.h"
#include "../shared/utils.h"
#include "../shared/names.h"
#include "../shared/colors.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

float calculate_expensive_trait_load(const Genome* genome);
float calculate_growth_cost_multiplier(const Colony* colony);
float calculate_survival_cost_multiplier(const Colony* colony);
float colony_spread_activity_factor(const Colony* colony);
float colony_toxin_output_factor(const Colony* colony);
float colony_turnover_factor(const Colony* colony);
float colony_signal_activity_factor(const Colony* colony);
void colony_update_persister_switching(Colony* colony);

#if defined(FEROX_SIMD_AVX2)
#include <immintrin.h>
#endif

#if defined(FEROX_SIMD_NEON)
#include <arm_neon.h>
#endif

// Direction offsets for 4-connectivity (N, E, S, W)
static const int DX[] = {0, 1, 0, -1};
static const int DY[] = {-1, 0, 1, 0};
static const int CARDINAL_DIR8_INDEX[] = {DIR_N, DIR_E, DIR_S, DIR_W};

#define COMBAT_CACHE_ENEMY_MASK 0x0Fu
#define COMBAT_CACHE_FRIENDLY_SHIFT 4u
#define COMBAT_CACHE_FRIENDLY_MASK 0x0Fu
#define COMBAT_CACHE_TICK_SHIFT 8u

static inline Colony* lookup_active_colony(World* world, uint32_t id) {
    if (!world || id == 0 || id >= world->colony_by_id_capacity) {
        return NULL;
    }
    Colony* colony = world->colony_by_id[id];
    return (colony && colony->active) ? colony : NULL;
}

static uint32_t lineage_root_id(World* world, const Colony* colony) {
    if (!world || !colony) return 0;

    uint32_t root_id = colony->id;
    uint32_t parent_id = colony->parent_id;
    int guard = 0;

    while (parent_id != 0 && guard < 64) {
        Colony* parent = lookup_active_colony(world, parent_id);
        if (!parent) {
            root_id = parent_id;
            break;
        }
        root_id = parent->id;
        parent_id = parent->parent_id;
        guard++;
    }

    return root_id;
}

static bool colonies_share_lineage(World* world, const Colony* a, const Colony* b) {
    if (!world || !a || !b) return false;
    if (a->id == b->id) return true;
    if (a->parent_id == b->id || b->parent_id == a->id) return true;
    uint32_t root_a = lineage_root_id(world, a);
    uint32_t root_b = lineage_root_id(world, b);
    return root_a != 0 && root_a == root_b;
}

static void update_hgt_cost_and_loss(World* world) {
    if (!world) return;

    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;

        float plasmid = utils_clamp_f(colony->hgt_plasmid_fraction, 0.0f, 1.0f);

        if (world->hgt_kinetics.enable_plasmid_loss && plasmid > 0.0f) {
            float loss = plasmid * world->hgt_kinetics.plasmid_loss_rate;
            float next_plasmid = utils_clamp_f(plasmid - loss, 0.0f, 1.0f);
            if (next_plasmid < plasmid) {
                colony->hgt_plasmid_loss_events++;
                world->hgt_metrics.plasmid_loss_events_total++;
            }
            plasmid = next_plasmid;
        }

        if (plasmid < 0.0001f) {
            plasmid = 0.0f;
            colony->hgt_is_transconjugant = false;
        }

        colony->hgt_plasmid_fraction = plasmid;
        colony->hgt_fitness_scale = 1.0f;
        if (world->hgt_kinetics.enable_plasmid_cost && plasmid > 0.0f) {
            float cost = world->hgt_kinetics.plasmid_cost_per_fraction * plasmid;
            colony->hgt_fitness_scale = utils_clamp_f(1.0f - cost, 0.2f, 1.0f);
        }
    }
}

// Direction offsets for 8-connectivity (N, NE, E, SE, S, SW, W, NW)
static const int DX8[] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int DY8[] = {-1, -1, 0, 1, 1, 1, 0, -1};
// Diagonal probability correction: 1.0 for cardinal, 1/sqrt(2) for diagonal
static const float DIR8_WEIGHT[] = {1.0f, 0.7071f, 1.0f, 0.7071f, 1.0f, 0.7071f, 1.0f, 0.7071f};

// Environmental constants
#define NUTRIENT_DEPLETION_RATE 0.05f   // Nutrients consumed per cell per tick
#define NUTRIENT_REGEN_RATE 0.002f      // Natural nutrient regeneration
#define QUORUM_SENSING_RADIUS 3         // Radius for local density calculation

static inline float monod_saturation(float substrate, float half_saturation) {
    if (substrate <= 0.0f) return 0.0f;
    if (half_saturation <= 0.0f) return 1.0f;
    return substrate / (half_saturation + substrate);
}

static inline float monod_growth_multiplier(const World* world, float substrate) {
    if (!world || !world->monod.enabled) return 1.0f;

    float saturation = monod_saturation(substrate, world->monod.half_saturation);
    float coupling = utils_clamp_f(world->monod.growth_coupling, 0.0f, 1.0f);
    return (1.0f - coupling) + coupling * saturation;
}

static inline float monod_uptake_multiplier(const World* world, float substrate) {
    if (!world || !world->monod.enabled) return 1.0f;

    float saturation = monod_saturation(substrate, world->monod.half_saturation);
    float uptake_min = utils_clamp_f(world->monod.uptake_min, 0.0f, 1.0f);
    float uptake_max = utils_clamp_f(world->monod.uptake_max, uptake_min, 1.0f);
    return uptake_min + (uptake_max - uptake_min) * saturation;
}

static const TransportModelParams DEFAULT_TRANSPORT_PARAMS = {
    .nutrient_diffusivity = 0.06f,
    .toxin_diffusivity = 0.08f,
    .signal_neighbor_transfer = 0.075f,
    .signal_decay = 0.10f,
    .eps_attenuation = 0.85f,
    .eps_exponent = 1.5f,
    .min_relative_diffusivity = 0.15f,
};

static TransportModelParams g_transport_params = {
    .nutrient_diffusivity = 0.06f,
    .toxin_diffusivity = 0.08f,
    .signal_neighbor_transfer = 0.075f,
    .signal_decay = 0.10f,
    .eps_attenuation = 0.85f,
    .eps_exponent = 1.5f,
    .min_relative_diffusivity = 0.15f,
};
static bool g_transport_params_overridden = false;

void simulation_set_transport_params(const TransportModelParams* params) {
    if (!params) return;

    g_transport_params.nutrient_diffusivity = utils_clamp_f(params->nutrient_diffusivity, 0.0f, 0.24f);
    g_transport_params.toxin_diffusivity = utils_clamp_f(params->toxin_diffusivity, 0.0f, 0.24f);
    g_transport_params.signal_neighbor_transfer = utils_clamp_f(params->signal_neighbor_transfer, 0.0f, 0.24f);
    g_transport_params.signal_decay = utils_clamp_f(params->signal_decay, 0.0f, 0.5f);
    g_transport_params.eps_attenuation = utils_clamp_f(params->eps_attenuation, 0.0f, 1.0f);
    g_transport_params.eps_exponent = utils_clamp_f(params->eps_exponent, 0.5f, 4.0f);
    g_transport_params.min_relative_diffusivity = utils_clamp_f(params->min_relative_diffusivity, 0.01f, 1.0f);
    g_transport_params_overridden = true;
}

void simulation_get_transport_params(TransportModelParams* out_params) {
    if (!out_params) return;
    *out_params = g_transport_params;
}

void simulation_reset_transport_params(void) {
    g_transport_params = DEFAULT_TRANSPORT_PARAMS;
    g_transport_params_overridden = false;
}

static inline float get_cell_eps_density(World* world, int idx) {
    uint32_t colony_id = world->cells[idx].colony_id;
    if (colony_id == 0) return 0.0f;

    Colony* colony = lookup_active_colony(world, colony_id);
    if (!colony) return 0.0f;
    if (colony->biofilm_strength <= 0.0f) return 0.0f;

    float eps = colony->biofilm_strength * (0.35f + colony->genome.biofilm_investment * 0.65f);
    return utils_clamp_f(eps, 0.0f, 1.0f);
}

static bool world_has_eps_barrier(const World* world) {
    if (!world || !world->colonies) {
        return false;
    }

    for (size_t i = 0; i < world->colony_count; i++) {
        const Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        if (colony->biofilm_strength > 0.0f) {
            return true;
        }
    }

    return false;
}

static bool build_cell_eps_cache(World* world, float* eps_cache, int total_cells) {
    if (!world || !eps_cache || total_cells <= 0) return false;
    if (!world_has_eps_barrier(world)) return false;

    bool has_eps = false;

    for (int i = 0; i < total_cells; i++) {
        float eps = get_cell_eps_density(world, i);
        eps_cache[i] = eps;
        has_eps = has_eps || eps > 0.0f;
    }

    return has_eps;
}

static inline float fast_eps_power(float value, float exponent) {
    if (value <= 0.0f) return 0.0f;

    if (fabsf(exponent - 1.0f) < 0.0001f) {
        return value;
    }
    if (fabsf(exponent - 1.5f) < 0.0001f) {
        return value * sqrtf(value);
    }
    if (fabsf(exponent - 2.0f) < 0.0001f) {
        return value * value;
    }
    if (fabsf(exponent - 3.0f) < 0.0001f) {
        return value * value * value;
    }
    if (fabsf(exponent - 4.0f) < 0.0001f) {
        float squared = value * value;
        return squared * squared;
    }

    return powf(value, exponent);
}

static inline float effective_diffusivity_scale(float eps_a, float eps_b) {
    float avg_eps = 0.5f * (eps_a + eps_b);
    float attenuation = 1.0f - g_transport_params.eps_attenuation *
                        fast_eps_power(avg_eps, g_transport_params.eps_exponent);
    attenuation = utils_clamp_f(attenuation,
                                g_transport_params.min_relative_diffusivity,
                                1.0f);
    return attenuation;
}

static inline float clamp_unit_interval(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

typedef struct {
    int idx;
    uint8_t enemy_mask;
    uint8_t friendly_count;
} ContestedBorder;

static inline uint32_t combat_cache_tick_tag(uint64_t tick) {
    return (uint32_t)(tick & 0x00FFFFFFu);
}

static inline uint32_t pack_combat_cache(uint32_t tick_tag,
                                         int friendly_count,
                                         uint8_t enemy_mask) {
    uint32_t encoded_friendly = (uint32_t)(friendly_count + 1) & COMBAT_CACHE_FRIENDLY_MASK;
    return (tick_tag << COMBAT_CACHE_TICK_SHIFT) |
           (encoded_friendly << COMBAT_CACHE_FRIENDLY_SHIFT) |
           ((uint32_t)enemy_mask & COMBAT_CACHE_ENEMY_MASK);
}

static inline int combat_cached_friendly_count(uint32_t packed_cache, uint32_t tick_tag) {
    if ((packed_cache >> COMBAT_CACHE_TICK_SHIFT) != tick_tag) {
        return -1;
    }

    uint32_t encoded = (packed_cache >> COMBAT_CACHE_FRIENDLY_SHIFT) & COMBAT_CACHE_FRIENDLY_MASK;
    if (encoded == 0) {
        return -1;
    }

    return (int)encoded - 1;
}

static bool append_contested_border(ContestedBorder** borders,
                                    int* count,
                                    int* capacity,
                                    int idx,
                                    uint8_t enemy_mask,
                                    int friendly_count) {
    if (!borders || !count || !capacity) {
        return false;
    }

    if (*count >= *capacity) {
        int new_capacity = (*capacity > 0) ? (*capacity * 2) : 128;
        ContestedBorder* resized = (ContestedBorder*)realloc(
            *borders, (size_t)new_capacity * sizeof(ContestedBorder));
        if (!resized) {
            return false;
        }
        *borders = resized;
        *capacity = new_capacity;
    }

    (*borders)[*count].idx = idx;
    (*borders)[*count].enemy_mask = enemy_mask;
    (*borders)[*count].friendly_count = (uint8_t)friendly_count;
    (*count)++;
    return true;
}

static uint8_t detect_enemy_neighbor_mask(const Cell* cells,
                                          int width,
                                          int height,
                                          int x,
                                          int y,
                                          uint32_t colony_id) {
    uint8_t mask = 0;

    for (int d = 0; d < 4; d++) {
        int nx = x + DX[d];
        int ny = y + DY[d];
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
            continue;
        }

        uint32_t neighbor_id = cells[ny * width + nx].colony_id;
        if (neighbor_id != 0 && neighbor_id != colony_id) {
            mask |= (uint8_t)(1u << d);
        }
    }

    return mask;
}

static void diffuse_scalar_field(World* world, float* field, float* scratch, float base_diffusivity) {
    if (!world || !field || !scratch || base_diffusivity <= 0.0f) return;

    const int width = world->width;
    const int height = world->height;
    const int total_cells = width * height;
    float* eps_cache = world->scratch_eps;
    if (!eps_cache) return;

    bool has_eps = build_cell_eps_cache(world, eps_cache, total_cells);

    if (width < 3 || height < 3) {
        if (!has_eps) {
            for (int y = 0; y < height; y++) {
                int row = y * width;
                for (int x = 0; x < width; x++) {
                    int idx = row + x;
                    float center = field[idx];
                    float flux_sum = 0.0f;

                    if (y > 0) flux_sum += field[idx - width] - center;
                    if (x + 1 < width) flux_sum += field[idx + 1] - center;
                    if (y + 1 < height) flux_sum += field[idx + width] - center;
                    if (x > 0) flux_sum += field[idx - 1] - center;

                    scratch[idx] = clamp_unit_interval(center + base_diffusivity * flux_sum);
                }
            }
        } else {
            for (int y = 0; y < height; y++) {
                int row = y * width;
                for (int x = 0; x < width; x++) {
                    int idx = row + x;
                    float center = field[idx];
                    float center_eps = eps_cache[idx];
                    float flux_sum = 0.0f;

                    if (y > 0) {
                        int ni = idx - width;
                        float scale = effective_diffusivity_scale(center_eps, eps_cache[ni]);
                        flux_sum += (field[ni] - center) * scale;
                    }
                    if (x + 1 < width) {
                        int ni = idx + 1;
                        float scale = effective_diffusivity_scale(center_eps, eps_cache[ni]);
                        flux_sum += (field[ni] - center) * scale;
                    }
                    if (y + 1 < height) {
                        int ni = idx + width;
                        float scale = effective_diffusivity_scale(center_eps, eps_cache[ni]);
                        flux_sum += (field[ni] - center) * scale;
                    }
                    if (x > 0) {
                        int ni = idx - 1;
                        float scale = effective_diffusivity_scale(center_eps, eps_cache[ni]);
                        flux_sum += (field[ni] - center) * scale;
                    }

                    scratch[idx] = clamp_unit_interval(center + base_diffusivity * flux_sum);
                }
            }
        }

        memcpy(field, scratch, (size_t)total_cells * sizeof(float));
        return;
    }

    if (!has_eps) {
        const float* top = field;
        float* top_dst = scratch;
        top_dst[0] = clamp_unit_interval(top[0] + base_diffusivity * (top[1] + field[width] - 2.0f * top[0]));
        for (int x = 1; x < width - 1; x++) {
            float center = top[x];
            float flux_sum = top[x - 1] + top[x + 1] + field[width + x] - 3.0f * center;
            top_dst[x] = clamp_unit_interval(center + base_diffusivity * flux_sum);
        }
        top_dst[width - 1] = clamp_unit_interval(
            top[width - 1] +
            base_diffusivity * (top[width - 2] + field[2 * width - 1] - 2.0f * top[width - 1]));

        for (int y = 1; y < height - 1; y++) {
            const float* prev = field + (y - 1) * width;
            const float* curr = field + y * width;
            const float* next = field + (y + 1) * width;
            float* dst = scratch + y * width;

            float center = curr[0];
            float flux_sum = prev[0] + curr[1] + next[0] - 3.0f * center;
            dst[0] = clamp_unit_interval(center + base_diffusivity * flux_sum);

            for (int x = 1; x < width - 1; x++) {
                center = curr[x];
                flux_sum = prev[x] + curr[x + 1] + next[x] + curr[x - 1] - 4.0f * center;
                dst[x] = clamp_unit_interval(center + base_diffusivity * flux_sum);
            }

            center = curr[width - 1];
            flux_sum = prev[width - 1] + next[width - 1] + curr[width - 2] - 3.0f * center;
            dst[width - 1] = clamp_unit_interval(center + base_diffusivity * flux_sum);
        }

        const float* bottom = field + (height - 1) * width;
        const float* above_bottom = field + (height - 2) * width;
        float* bottom_dst = scratch + (height - 1) * width;
        bottom_dst[0] = clamp_unit_interval(
            bottom[0] + base_diffusivity * (above_bottom[0] + bottom[1] - 2.0f * bottom[0]));
        for (int x = 1; x < width - 1; x++) {
            float center = bottom[x];
            float flux_sum = above_bottom[x] + bottom[x + 1] + bottom[x - 1] - 3.0f * center;
            bottom_dst[x] = clamp_unit_interval(center + base_diffusivity * flux_sum);
        }
        bottom_dst[width - 1] = clamp_unit_interval(
            bottom[width - 1] +
            base_diffusivity * (above_bottom[width - 1] + bottom[width - 2] - 2.0f * bottom[width - 1]));

        memcpy(field, scratch, (size_t)total_cells * sizeof(float));
        return;
    }

    const float* top = field;
    const float* top_eps = eps_cache;
    float* top_dst = scratch;
    {
        float center = top[0];
        float center_eps = top_eps[0];
        float flux_sum = 0.0f;
        flux_sum += (top[1] - center) * effective_diffusivity_scale(center_eps, top_eps[1]);
        flux_sum += (field[width] - center) * effective_diffusivity_scale(center_eps, eps_cache[width]);
        top_dst[0] = clamp_unit_interval(center + base_diffusivity * flux_sum);
    }
    for (int x = 1; x < width - 1; x++) {
        float center = top[x];
        float center_eps = top_eps[x];
        float flux_sum = 0.0f;
        flux_sum += (top[x - 1] - center) * effective_diffusivity_scale(center_eps, top_eps[x - 1]);
        flux_sum += (top[x + 1] - center) * effective_diffusivity_scale(center_eps, top_eps[x + 1]);
        flux_sum += (field[width + x] - center) * effective_diffusivity_scale(center_eps, eps_cache[width + x]);
        top_dst[x] = clamp_unit_interval(center + base_diffusivity * flux_sum);
    }
    {
        int x = width - 1;
        float center = top[x];
        float center_eps = top_eps[x];
        float flux_sum = 0.0f;
        flux_sum += (top[x - 1] - center) * effective_diffusivity_scale(center_eps, top_eps[x - 1]);
        flux_sum += (field[width + x] - center) * effective_diffusivity_scale(center_eps, eps_cache[width + x]);
        top_dst[x] = clamp_unit_interval(center + base_diffusivity * flux_sum);
    }

    for (int y = 1; y < height - 1; y++) {
        const float* prev = field + (y - 1) * width;
        const float* curr = field + y * width;
        const float* next = field + (y + 1) * width;
        const float* prev_eps = eps_cache + (y - 1) * width;
        const float* curr_eps = eps_cache + y * width;
        const float* next_eps = eps_cache + (y + 1) * width;
        float* dst = scratch + y * width;

        float center = curr[0];
        float center_eps = curr_eps[0];
        float flux_sum = 0.0f;
        flux_sum += (prev[0] - center) * effective_diffusivity_scale(center_eps, prev_eps[0]);
        flux_sum += (curr[1] - center) * effective_diffusivity_scale(center_eps, curr_eps[1]);
        flux_sum += (next[0] - center) * effective_diffusivity_scale(center_eps, next_eps[0]);
        dst[0] = clamp_unit_interval(center + base_diffusivity * flux_sum);

        for (int x = 1; x < width - 1; x++) {
            center = curr[x];
            center_eps = curr_eps[x];
            flux_sum = 0.0f;
            flux_sum += (prev[x] - center) * effective_diffusivity_scale(center_eps, prev_eps[x]);
            flux_sum += (curr[x + 1] - center) * effective_diffusivity_scale(center_eps, curr_eps[x + 1]);
            flux_sum += (next[x] - center) * effective_diffusivity_scale(center_eps, next_eps[x]);
            flux_sum += (curr[x - 1] - center) * effective_diffusivity_scale(center_eps, curr_eps[x - 1]);
            dst[x] = clamp_unit_interval(center + base_diffusivity * flux_sum);
        }

        center = curr[width - 1];
        center_eps = curr_eps[width - 1];
        flux_sum = 0.0f;
        flux_sum += (prev[width - 1] - center) * effective_diffusivity_scale(center_eps, prev_eps[width - 1]);
        flux_sum += (next[width - 1] - center) * effective_diffusivity_scale(center_eps, next_eps[width - 1]);
        flux_sum += (curr[width - 2] - center) * effective_diffusivity_scale(center_eps, curr_eps[width - 2]);
        dst[width - 1] = clamp_unit_interval(center + base_diffusivity * flux_sum);
    }

    const float* bottom = field + (height - 1) * width;
    const float* above_bottom = field + (height - 2) * width;
    const float* bottom_eps = eps_cache + (height - 1) * width;
    const float* above_bottom_eps = eps_cache + (height - 2) * width;
    float* bottom_dst = scratch + (height - 1) * width;
    {
        float center = bottom[0];
        float center_eps = bottom_eps[0];
        float flux_sum = 0.0f;
        flux_sum += (above_bottom[0] - center) *
                    effective_diffusivity_scale(center_eps, above_bottom_eps[0]);
        flux_sum += (bottom[1] - center) *
                    effective_diffusivity_scale(center_eps, bottom_eps[1]);
        bottom_dst[0] = clamp_unit_interval(center + base_diffusivity * flux_sum);
    }
    for (int x = 1; x < width - 1; x++) {
        float center = bottom[x];
        float center_eps = bottom_eps[x];
        float flux_sum = 0.0f;
        flux_sum += (above_bottom[x] - center) *
                    effective_diffusivity_scale(center_eps, above_bottom_eps[x]);
        flux_sum += (bottom[x + 1] - center) *
                    effective_diffusivity_scale(center_eps, bottom_eps[x + 1]);
        flux_sum += (bottom[x - 1] - center) *
                    effective_diffusivity_scale(center_eps, bottom_eps[x - 1]);
        bottom_dst[x] = clamp_unit_interval(center + base_diffusivity * flux_sum);
    }
    {
        int x = width - 1;
        float center = bottom[x];
        float center_eps = bottom_eps[x];
        float flux_sum = 0.0f;
        flux_sum += (above_bottom[x] - center) *
                    effective_diffusivity_scale(center_eps, above_bottom_eps[x]);
        flux_sum += (bottom[x - 1] - center) *
                    effective_diffusivity_scale(center_eps, bottom_eps[x - 1]);
        bottom_dst[x] = clamp_unit_interval(center + base_diffusivity * flux_sum);
    }

    memcpy(field, scratch, (size_t)(width * height) * sizeof(float));
}

// SIMD helpers for dense float-array updates in per-tick environment passes.
#if defined(FEROX_SIMD_AVX2) && (defined(__x86_64__) || defined(__i386__))
static inline bool simd_avx2_runtime_available(void) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}

__attribute__((target("avx2")))
static void simd_mul_inplace_avx2(float* values, int count, float factor) {
    const __m256 vfactor = _mm256_set1_ps(factor);
    int i = 0;
    for (; i + 8 <= count; i += 8) {
        __m256 v = _mm256_loadu_ps(values + i);
        v = _mm256_mul_ps(v, vfactor);
        _mm256_storeu_ps(values + i, v);
    }
    for (; i < count; i++) {
        values[i] *= factor;
    }
}

__attribute__((target("avx2")))
static void simd_clamp01_copy_avx2(float* dst, const float* src, int count) {
    const __m256 vzero = _mm256_set1_ps(0.0f);
    const __m256 vone = _mm256_set1_ps(1.0f);
    int i = 0;
    for (; i + 8 <= count; i += 8) {
        __m256 v = _mm256_loadu_ps(src + i);
        v = _mm256_max_ps(v, vzero);
        v = _mm256_min_ps(v, vone);
        _mm256_storeu_ps(dst + i, v);
    }
    for (; i < count; i++) {
        dst[i] = utils_clamp_f(src[i], 0.0f, 1.0f);
    }
}
#endif

static void simd_mul_inplace(float* values, int count, float factor) {
#if defined(FEROX_SIMD_AVX2) && (defined(__x86_64__) || defined(__i386__))
    if (simd_avx2_runtime_available()) {
        simd_mul_inplace_avx2(values, count, factor);
        return;
    }
#elif defined(FEROX_SIMD_NEON)
    const float32x4_t vfactor = vdupq_n_f32(factor);
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t v = vld1q_f32(values + i);
        v = vmulq_f32(v, vfactor);
        vst1q_f32(values + i, v);
    }
    for (; i < count; i++) {
        values[i] *= factor;
    }
    return;
#endif

    for (int i = 0; i < count; i++) {
        values[i] *= factor;
    }
}

static void simd_clamp01_copy(float* dst, const float* src, int count) {
#if defined(FEROX_SIMD_AVX2) && (defined(__x86_64__) || defined(__i386__))
    if (simd_avx2_runtime_available()) {
        simd_clamp01_copy_avx2(dst, src, count);
        return;
    }
#elif defined(FEROX_SIMD_NEON)
    const float32x4_t vzero = vdupq_n_f32(0.0f);
    const float32x4_t vone = vdupq_n_f32(1.0f);
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t v = vld1q_f32(src + i);
        v = vmaxq_f32(v, vzero);
        v = vminq_f32(v, vone);
        vst1q_f32(dst + i, v);
    }
    for (; i < count; i++) {
        dst[i] = utils_clamp_f(src[i], 0.0f, 1.0f);
    }
    return;
#endif

    for (int i = 0; i < count; i++) {
        dst[i] = utils_clamp_f(src[i], 0.0f, 1.0f);
    }
}

static void apply_diffusion_decay(World* world, float* layer, const RDFieldControl* control, bool clamp01) {
    if (!world || !layer || !control || !world->scratch_signals) {
        return;
    }

    const int width = world->width;
    const int height = world->height;
    const int total = width * height;
    const float diffusion = control->diffusion;
    const float decay = control->decay;

    if (decay > 0.0f && diffusion == 0.0f) {
        simd_mul_inplace(layer, total, 1.0f - decay);
        if (clamp01) {
            simd_clamp01_copy(layer, layer, total);
        }
        return;
    }

    if (decay == 0.0f && diffusion == 0.0f) {
        if (clamp01) {
            simd_clamp01_copy(layer, layer, total);
        }
        return;
    }

    float* scratch = world->scratch_signals;
    memset(scratch, 0, (size_t)total * sizeof(float));

    const float center = 1.0f - decay - 4.0f * diffusion;
    for (int y = 0; y < height; y++) {
        int row = y * width;
        for (int x = 0; x < width; x++) {
            int idx = row + x;
            float next = layer[idx] * center;

            if (y > 0) next += layer[idx - width] * diffusion;
            if (x + 1 < width) next += layer[idx + 1] * diffusion;
            if (y + 1 < height) next += layer[idx + width] * diffusion;
            if (x > 0) next += layer[idx - 1] * diffusion;

            scratch[idx] = next;
        }
    }

    if (clamp01) {
        simd_clamp01_copy(layer, scratch, total);
    } else {
        memcpy(layer, scratch, (size_t)total * sizeof(float));
    }
}

// Forward declarations
static float get_scent_influence(World* world, int x, int y, int dx, int dy, 
                                  uint32_t colony_id, const Genome* genome);
static void attempt_horizontal_gene_transfer(World* world, Colony* donor, Colony* recipient);

// Calculate local population density around a cell
static float calculate_local_density(World* world, int x, int y, uint32_t colony_id) {
    if (!world || !world->cells) return 0.0f;

    int min_y = y - QUORUM_SENSING_RADIUS;
    int max_y = y + QUORUM_SENSING_RADIUS;
    if (min_y < 0) min_y = 0;
    if (max_y >= world->height) max_y = world->height - 1;

    int min_x = x - QUORUM_SENSING_RADIUS;
    int max_x = x + QUORUM_SENSING_RADIUS;
    if (min_x < 0) min_x = 0;
    if (max_x >= world->width) max_x = world->width - 1;

    int count = 0;
    int total = 0;
    for (int ny = min_y; ny <= max_y; ny++) {
        int row = ny * world->width;
        for (int nx = min_x; nx <= max_x; nx++) {
            const Cell* neighbor = &world->cells[row + nx];
            total++;
            if (neighbor->colony_id == colony_id) {
                count++;
            }
        }
    }
    return total > 0 ? (float)count / (float)total : 0.0f;
}

// Calculate environmental spread modifier for a target cell
static float calculate_env_spread_modifier(World* world,
                                           Colony* colony,
                                           int tx,
                                           int ty,
                                           float local_density) {
    int target_idx = ty * world->width + tx;
    float modifier = 1.0f;
    
    // Chemotaxis: prefer higher nutrient levels
    float nutrient = world->nutrients[target_idx];
    modifier *= (1.0f + colony->genome.nutrient_sensitivity * (nutrient - 0.5f));
    
    // Toxin avoidance: avoid areas with toxins
    float toxin = world->toxins[target_idx];
    modifier *= (1.0f - colony->genome.toxin_sensitivity * toxin);
    
    // Edge preference: positive = prefer edges, negative = prefer center
    float edge_dist_x = fminf((float)tx, (float)(world->width - 1 - tx)) / (float)(world->width / 2);
    float edge_dist_y = fminf((float)ty, (float)(world->height - 1 - ty)) / (float)(world->height / 2);
    float edge_factor = 1.0f - fminf(edge_dist_x, edge_dist_y);
    modifier *= (1.0f + colony->genome.edge_affinity * (edge_factor - 0.5f));
    
    // Quorum sensing: reduce spread probability if local density exceeds threshold
    if (local_density > colony->genome.quorum_threshold) {
        float density_penalty = (local_density - colony->genome.quorum_threshold) * 
                                (1.0f - colony->genome.density_tolerance);
        modifier *= (1.0f - density_penalty);
    }
    
    return utils_clamp_f(modifier, 0.3f, 2.0f);  // Minimum floor of 0.3 to prevent stalling
}

// Stack for iterative flood-fill
typedef struct {
    int* data;
    int top;
    int capacity;
} Stack;

static Stack* stack_create(int capacity) {
    Stack* s = (Stack*)malloc(sizeof(Stack));
    if (!s) return NULL;
    s->data = (int*)malloc(capacity * 2 * sizeof(int));  // x,y pairs
    s->top = 0;
    s->capacity = capacity * 2;
    return s;
}

static void stack_destroy(Stack* s) {
    if (s) {
        free(s->data);
        free(s);
    }
}

static void stack_push(Stack* s, int x, int y) {
    if (s->top + 2 > s->capacity) {
        int new_capacity = s->capacity * 2;
        int* new_data = (int*)realloc(s->data, new_capacity * sizeof(int));
        if (!new_data) {
            // Realloc failed - cannot push, this will cause flood-fill to be incomplete
            // but avoids a crash. The caller should handle incomplete results.
            return;
        }
        s->data = new_data;
        s->capacity = new_capacity;
    }
    s->data[s->top++] = x;
    s->data[s->top++] = y;
}

static bool stack_pop(Stack* s, int* x, int* y) {
    if (s->top < 2) return false;
    *y = s->data[--s->top];
    *x = s->data[--s->top];
    return true;
}

static bool stack_empty(Stack* s) {
    return s->top == 0;
}

// Flood-fill from a starting cell, marking all connected cells with component_id
static int flood_fill(World* world, int start_x, int start_y, uint32_t colony_id, int8_t comp_id) {
    Stack* stack = stack_create(world->width * world->height / 4);
    if (!stack) return 0;
    
    int count = 0;
    stack_push(stack, start_x, start_y);
    
    Cell* start_cell = world_get_cell(world, start_x, start_y);
    if (start_cell) {
        start_cell->component_id = comp_id;
    }
    
    while (!stack_empty(stack)) {
        int x, y;
        stack_pop(stack, &x, &y);
        count++;
        
        // Check all 8 neighbors (Moore neighborhood, matches spread connectivity)
        for (int d = 0; d < 8; d++) {
            int nx = x + DX8[d];
            int ny = y + DY8[d];
            
            Cell* neighbor = world_get_cell(world, nx, ny);
            if (neighbor && neighbor->colony_id == colony_id && neighbor->component_id == -1) {
                neighbor->component_id = comp_id;
                stack_push(stack, nx, ny);
            }
        }
    }
    
    stack_destroy(stack);
    return count;
}

int* find_connected_components(World* world, uint32_t colony_id, int* num_components) {
    if (!world || !num_components || colony_id == 0) {
        if (num_components) *num_components = 0;
        return NULL;
    }
    
    // Reset component markers for this colony's cells
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == colony_id) {
            world->cells[i].component_id = -1;
        }
    }
    
    // Find components
    // Note: component_id is int8_t (-128 to 127), so we can track at most 127 components (0-126)
    // In practice, colonies rarely have more than a few components
    const int MAX_COMPONENTS = 127;
    int* sizes = NULL;
    int count = 0;
    int capacity = 4;
    sizes = (int*)malloc(capacity * sizeof(int));
    if (!sizes) {
        *num_components = 0;
        return NULL;
    }
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (cell && cell->colony_id == colony_id && cell->component_id == -1) {
                // Start new component - but stop if we hit the int8_t limit
                if (count >= MAX_COMPONENTS) {
                    // Too many components to track safely, return what we have
                    // Remaining cells will be processed on next tick
                    *num_components = count;
                    return sizes;
                }
                if (count >= capacity) {
                    int new_capacity = capacity * 2;
                    if (new_capacity > MAX_COMPONENTS) new_capacity = MAX_COMPONENTS;
                    int* new_sizes = (int*)realloc(sizes, new_capacity * sizeof(int));
                    if (!new_sizes) {
                        // Realloc failed - return what we have
                        *num_components = count;
                        return sizes;
                    }
                    sizes = new_sizes;
                    capacity = new_capacity;
                }
                sizes[count] = flood_fill(world, x, y, colony_id, (int8_t)count);
                count++;
            }
        }
    }
    
    *num_components = count;
    return sizes;
}

// Count friendly neighbors around a cell (for flanking calculation)
static int count_friendly_neighbors(World* world, int x, int y, uint32_t colony_id) {
    if (!world || !world->cells) return 0;

    int count = 0;

    int min_y = y - 1;
    int max_y = y + 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= world->height) max_y = world->height - 1;

    int min_x = x - 1;
    int max_x = x + 1;
    if (min_x < 0) min_x = 0;
    if (max_x >= world->width) max_x = world->width - 1;

    for (int ny = min_y; ny <= max_y; ny++) {
        int row = ny * world->width;
        for (int nx = min_x; nx <= max_x; nx++) {
            if (nx == x && ny == y) continue;
            if (world->cells[row + nx].colony_id == colony_id) {
                count++;
            }
        }
    }

    return count;
}

// Calculate local biomass density for cooperative propagation
// Based on Ben-Jacob model: Db = D0 * b^k where k=1 (linear cooperative)
// Higher local density = more mechanical pushing = faster spread
static float calculate_biomass_pressure(World* world, int x, int y, uint32_t colony_id) {
    if (!world || !world->cells) return 1.0f;

    int same_count = 0;
    int total = 0;

    int min_y = y - 1;
    int max_y = y + 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= world->height) max_y = world->height - 1;

    int min_x = x - 1;
    int max_x = x + 1;
    if (min_x < 0) min_x = 0;
    if (max_x >= world->width) max_x = world->width - 1;

    for (int ny = min_y; ny <= max_y; ny++) {
        int row = ny * world->width;
        for (int nx = min_x; nx <= max_x; nx++) {
            if (nx == x && ny == y) continue;
            total++;
            if (world->cells[row + nx].colony_id == colony_id) {
                same_count++;
            }
        }
    }
    
    if (total == 0) return 1.0f;
    
    // Cooperative propagation: more neighbors = more pushing force
    // Moderate effect to avoid uniform wavefront advancement
    float density = (float)same_count / (float)total;
    return 1.0f + density * 0.5f;  // Up to 1.5x spread at full density
}

typedef struct {
    int friendly_count;
    int enemy_count;
} TargetNeighborhoodStats;

static TargetNeighborhoodStats analyze_target_neighborhood(const World* world,
                                                           int tx,
                                                           int ty,
                                                           uint32_t colony_id) {
    TargetNeighborhoodStats stats = {0, 0};
    if (!world || !world->cells) {
        return stats;
    }

    for (int d = 0; d < 8; d++) {
        int nx = tx + DX8[d];
        int ny = ty + DY8[d];
        if (nx < 0 || nx >= world->width || ny < 0 || ny >= world->height) {
            continue;
        }

        uint32_t neighbor_id = world->cells[ny * world->width + nx].colony_id;
        if (neighbor_id == colony_id) {
            stats.friendly_count++;
        } else if (neighbor_id != 0) {
            stats.enemy_count++;
        }
    }

    return stats;
}

// Quorum activation level based on accumulated colony signal
// 0 = below threshold, 1 = strongly above threshold
static float get_quorum_activation(const Colony* colony) {
    if (!colony) return 0.0f;
    float threshold = colony->genome.quorum_threshold;
    if (colony->signal_strength <= threshold) return 0.0f;
    return utils_clamp_f(
        (colony->signal_strength - threshold) / (1.0f - threshold + 0.001f),
        0.0f, 1.0f
    );
}

// Perception-based directional modifier.
// Colonies with higher detection_range scan ahead in each direction to find
// nutrients and threats, biasing spread toward favorable areas.
// Returns a multiplier (0.5-2.0) for the given direction.
static float calculate_perception_modifier(World* world, int x, int y,
                                            int dx, int dy, Colony* colony) {
    float range = colony->genome.detection_range;
    if (range < 0.05f) return 1.0f;  // No perception, no bias

    // Scan distance: 2 to 8 cells depending on detection_range
    int scan_dist = 2 + (int)(range * 6.0f);

    float nutrient_sum = 0.0f;
    float empty_count = 0.0f;
    float enemy_count = 0.0f;
    int samples = 0;

    for (int step = 1; step <= scan_dist; step++) {
        int sx = x + dx * step;
        int sy = y + dy * step;
        if (sx < 0 || sx >= world->width || sy < 0 || sy >= world->height) break;
        int idx = sy * world->width + sx;
        nutrient_sum += world->nutrients[idx];
        if (world->cells[idx].colony_id == 0)
            empty_count += 1.0f;
        else if (world->cells[idx].colony_id != colony->id)
            enemy_count += 1.0f;
        samples++;
    }

    if (samples == 0) return 1.0f;

    float inv = 1.0f / samples;
    float nutrient_score = nutrient_sum * inv;         // 0-1: avg nutrient ahead
    float space_score = empty_count * inv;             // 0-1: fraction empty ahead
    float threat_score = enemy_count * inv;            // 0-1: fraction enemy ahead

    // Nutrient-seeking: boost directions with more nutrients
    float nutrient_boost = 1.0f + colony->genome.nutrient_sensitivity * nutrient_score * 0.5f;

    // Space-seeking: prefer directions with more room
    float space_boost = 1.0f + space_score * 0.3f;

    // Threat response: aggressive colonies move toward enemies, defensive ones avoid
    float threat_mod = 1.0f;
    if (threat_score > 0.0f) {
        if (colony->genome.aggression > 0.5f) {
            // Aggressive: attracted to enemies
            threat_mod = 1.0f + (colony->genome.aggression - 0.5f) * threat_score * 0.6f;
        } else {
            // Defensive: repelled by enemies
            threat_mod = 1.0f - (0.5f - colony->genome.aggression) * threat_score * 0.4f;
        }
    }

    float result = nutrient_boost * space_boost * threat_mod;
    // Clamp to reasonable range
    if (result < 0.5f) result = 0.5f;
    if (result > 2.0f) result = 2.0f;
    return result;
}

void simulation_spread(World* world) {
    if (!world) return;

    const int width = world->width;
    Cell* cells = world->cells;
    float* nutrients = world->nutrients;

    // Create list of cells to colonize (avoid modifying while iterating)
    typedef struct { int x, y; uint32_t colony_id; } PendingCell;
    PendingCell* pending = NULL;
    int pending_count = 0;
    int pending_capacity = 64;
    pending = (PendingCell*)malloc(pending_capacity * sizeof(PendingCell));
    if (!pending) return;
    
    for (int y = 0; y < world->height; y++) {
        int row = y * width;
        for (int x = 0; x < width; x++) {
            int idx = row + x;
            Cell* cell = &cells[idx];
            if (cell->colony_id == 0) continue;

            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony) continue;

            float biomass_pressure = calculate_biomass_pressure(world, x, y, cell->colony_id);
            float quorum_activation = get_quorum_activation(colony);
            float quorum_boost = 1.0f + quorum_activation * colony->genome.motility * 0.8f;
            float dormancy_factor = colony_spread_activity_factor(colony);
            float trait_growth_cost = calculate_growth_cost_multiplier(colony);
            float growth_uptake = monod_growth_multiplier(world, nutrients[idx]);
            bool use_quorum_density = colony->genome.density_tolerance < 0.999f;
            float local_density = use_quorum_density ? calculate_local_density(world, x, y, colony->id) : 0.0f;
            bool use_env_modifier =
                fabsf(colony->genome.nutrient_sensitivity) > 0.0001f ||
                fabsf(colony->genome.toxin_sensitivity) > 0.0001f ||
                fabsf(colony->genome.edge_affinity) > 0.0001f ||
                (use_quorum_density && colony->genome.quorum_threshold < 0.999f);
            bool use_scent = (colony->genome.density_tolerance < 0.999f) ||
                             (fabsf((colony->genome.aggression - colony->genome.defense_priority) *
                                    colony->genome.signal_sensitivity) > 0.0001f);
            bool use_perception = colony->genome.detection_range >= 0.05f;

            // Try to spread to 8 neighbors (Moore neighborhood) for organic shapes
            // Diagonal moves use 1/√2 correction for isotropic growth
            for (int d = 0; d < 8; d++) {
                int nx = x + DX8[d];
                int ny = y + DY8[d];

                if (nx < 0 || nx >= width || ny < 0 || ny >= world->height) continue;

                Cell* neighbor = &cells[ny * width + nx];
                if (neighbor->colony_id == 0) {
                    // Empty cell - calculate spread probability with environmental sensing
                    float env_modifier = use_env_modifier ?
                        calculate_env_spread_modifier(world, colony, nx, ny, local_density) :
                        1.0f;

                    // Scent influence - react to nearby colonies
                    float scent_modifier = use_scent ?
                        get_scent_influence(world, x, y, DX8[d], DY8[d], cell->colony_id, &colony->genome) :
                        1.0f;
                    TargetNeighborhoodStats target_stats = analyze_target_neighborhood(world, nx, ny, cell->colony_id);

                    // Strategic spread: push harder towards open space, less where enemies are
                    float strategic_modifier = 1.0f;
                    if (target_stats.enemy_count > 0) {
                        strategic_modifier *= (0.3f + colony->genome.aggression * 0.4f);
                    }

                    // Success history affects spread direction
                    float history_bonus = 1.0f + colony->success_history[d] * 0.3f;

                    // Curvature smoothing: prefer filling concavities for smooth edges
                    float curvature = 0.85f + (float)target_stats.friendly_count * 0.15f;

                    // Diagonal isotropy correction: 1/√2 for diagonals
                    float iso_correction = DIR8_WEIGHT[d];

                    // Per-cell stochastic noise for organic/irregular edges (Eden model)
                    // Without this, all border cells grow at the same rate → flat fronts
                    float noise = 0.6f + rand_float() * 0.8f;  // 0.6-1.4x random variation

                    // Perception: look ahead in this direction for nutrients/threats/space
                    float perception = use_perception ?
                        calculate_perception_modifier(world, x, y, DX8[d], DY8[d], colony) :
                        1.0f;

                    float spread_prob = colony->genome.spread_rate * colony->genome.metabolism * 
                                        env_modifier * colony->genome.spread_weights[d] * scent_modifier *
                                        strategic_modifier * history_bonus * biomass_pressure *
                                        quorum_boost * dormancy_factor * trait_growth_cost * curvature *
                                        iso_correction * noise * perception * growth_uptake *
                                        colony->hgt_fitness_scale * 2.0f;
                    
                    if (rand_float() < spread_prob) {
                        if (pending_count >= pending_capacity) {
                            pending_capacity *= 2;
                            PendingCell* new_pending = (PendingCell*)realloc(
                                pending, pending_capacity * sizeof(PendingCell)
                            );
                            if (!new_pending) {
                                free(pending);
                                return;
                            }
                            pending = new_pending;
                        }
                        pending[pending_count++] = (PendingCell){nx, ny, cell->colony_id};
                    }
                } else if (neighbor->colony_id != cell->colony_id) {
                    Colony* enemy = world_get_colony(world, neighbor->colony_id);
                    if (enemy && enemy->active) {
                        attempt_horizontal_gene_transfer(world, colony, enemy);
                        attempt_horizontal_gene_transfer(world, enemy, colony);
                    }
                }
            }
        }
    }
    
    // Apply pending colonizations - this is where new cells are "born"
    // Mutations happen during cell division (new cell creation)
    for (int i = 0; i < pending_count; i++) {
        Cell* cell = world_get_cell(world, pending[i].x, pending[i].y);
        if (cell) {
            uint32_t old_colony = cell->colony_id;
            
            // Update old colony's cell count
            if (old_colony != 0) {
                Colony* old = world_get_colony(world, old_colony);
                if (old && old->cell_count > 0) {
                    old->cell_count--;
                }
            }
            
            // Colonize
            cell->colony_id = pending[i].colony_id;
            cell->age = 0;
            
            // Update new colony's cell count and potentially mutate
            Colony* colony = world_get_colony(world, pending[i].colony_id);
            if (colony) {
                colony->cell_count++;
                
                // MUTATION ON REPRODUCTION: Each new cell has a chance to cause colony mutation
                // Higher stress = more mutations (adaptation pressure)
                float mutation_chance = colony->genome.mutation_rate * 
                                        (1.0f + colony->stress_level * 2.0f);
                if (rand_float() < mutation_chance) {
                    genome_mutate(&colony->genome);
                }
            }
        }
    }
    
    free(pending);
}

void simulation_update_hgt_kinetics(World* world) {
    update_hgt_cost_and_loss(world);
}

void simulation_mutate(World* world) {
    // Mutations happen during cell division AND as constant background process
    // This ensures evolution is always visible and dynamic
    if (!world) return;
    
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        
        // Higher baseline mutation - evolution should be visible
        // Minimum 8% chance + mutation_rate contribution
        float baseline_rate = 0.08f + colony->genome.mutation_rate * 0.6f;
        
        // Stressed colonies mutate much more as they try to adapt
        baseline_rate *= (1.0f + colony->stress_level * 2.5f);
        
        // Larger colonies have more chances to mutate (more cells dividing)
        baseline_rate *= (1.0f + (float)colony->cell_count / 300.0f);
        
        if (rand_float() < baseline_rate) {
            // Store original genome for speciation check
            Genome original = colony->genome;
            
            // Apply mutation
            genome_mutate(&colony->genome);
            
            // Update colony color to reflect genome changes
            colony->color = colony->genome.body_color;
            
            // SPECIATION EVENT: If mutation was extreme, create a new species
            // Check how different the genome became
            float genetic_distance = genome_distance(&original, &colony->genome);
            
            // 2% base chance of speciation, higher only for very dramatic mutations
            float speciation_chance = 0.02f + genetic_distance * 0.15f;
            
            // Only larger colonies can speciate (need critical mass)
            if (colony->cell_count < 20) {
                speciation_chance = 0.0f;
            }
            
            if (rand_float() < speciation_chance) {
                // SPECIATION! Start from a single seed cell and grow outward
                // Find a random border cell to be the seed
                int seed_x = -1, seed_y = -1;
                int attempts = 0;
                while (seed_x < 0 && attempts < 100) {
                    int j = rand() % (world->width * world->height);
                    Cell* cell = &world->cells[j];
                    if (cell->colony_id == colony->id && cell->is_border) {
                        seed_x = j % world->width;
                        seed_y = j / world->width;
                    }
                    attempts++;
                }
                
                if (seed_x >= 0) {
                    // Transfer 5-15% of cells, but start from seed and grow outward
                    int cells_for_new = (int)(colony->cell_count * (0.05f + rand_float() * 0.1f));
                    cells_for_new = (cells_for_new < 3) ? 3 : (cells_for_new > 20) ? 20 : cells_for_new;
                    
                    // Create new colony with mutated genome
                    Colony new_species;
                    memset(&new_species, 0, sizeof(Colony));
                    new_species.genome = colony->genome;  // Mutated genome goes to new species
                    new_species.color = new_species.genome.body_color;
                    new_species.active = true;
                    new_species.parent_id = colony->id;
                    new_species.shape_seed = colony->shape_seed ^ (uint32_t)(rand() << 8);
                    new_species.wobble_phase = rand_float() * 6.28f;
                    new_species.cell_count = 0;
                    new_species.max_cell_count = 0;
                    new_species.hgt_plasmid_fraction = colony->hgt_plasmid_fraction;
                    new_species.hgt_is_transconjugant = colony->hgt_is_transconjugant;
                    generate_scientific_name(new_species.name, sizeof(new_species.name));
                    
                    // Revert parent to original genome
                    colony->genome = original;
                    colony->color = original.body_color;
                    
                    uint32_t new_id = world_add_colony(world, new_species);
                    if (new_id > 0) {
                        // BFS from seed cell to transfer contiguous cells
                        int transferred = 0;
                        int q_cap = 1024;
                        int* queue = (int*)malloc(q_cap * sizeof(int));
                        if (!queue) continue;
                        int q_front = 0, q_back = 0;
                        queue[q_back++] = seed_y * world->width + seed_x;
                        
                        while (q_front < q_back && transferred < cells_for_new) {
                            int idx = queue[q_front++];
                            Cell* cell = &world->cells[idx];
                            if (cell->colony_id != colony->id) continue;
                            
                            // Transfer this cell
                            cell->colony_id = new_id;
                            cell->age = 0;
                            transferred++;
                            if (colony->cell_count > 0) colony->cell_count--;
                            
                            // Add neighbors to queue
                            int cx = idx % world->width;
                            int cy = idx / world->width;
                            int dx[] = {-1, 1, 0, 0};
                            int dy[] = {0, 0, -1, 1};
                            for (int d = 0; d < 4; d++) {
                                int nx = cx + dx[d];
                                int ny = cy + dy[d];
                                if (nx >= 0 && nx < world->width && ny >= 0 && ny < world->height) {
                                    int nidx = ny * world->width + nx;
                                    if (world->cells[nidx].colony_id == colony->id) {
                                        if (q_back >= q_cap) {
                                            q_cap *= 2;
                                            int* nq = (int*)realloc(queue, q_cap * sizeof(int));
                                            if (!nq) break;
                                            queue = nq;
                                        }
                                        queue[q_back++] = nidx;
                                    }
                                }
                            }
                        }
                        
                        free(queue);
                        
                        // Update new species cell count
                        Colony* new_col = world_get_colony(world, new_id);
                        if (new_col) {
                            new_col->cell_count = transferred;
                            new_col->max_cell_count = transferred;
                        }
                    }
                }
            }
        }
    }
}

void simulation_check_divisions(World* world) {
    if (!world) return;
    
    // Only process one division per tick to keep simulation stable
    bool division_occurred = false;
    
    for (size_t i = 0; i < world->colony_count && !division_occurred; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active || colony->cell_count < 2) continue;
        
        int num_components;
        int* sizes = find_connected_components(world, colony->id, &num_components);
        
        if (sizes && num_components > 1) {
            // Colony has split! Keep largest component, create new colonies for others
            int largest_idx = 0;
            int largest_size = sizes[0];
            for (int c = 1; c < num_components; c++) {
                if (sizes[c] > largest_size) {
                    largest_size = sizes[c];
                    largest_idx = c;
                }
            }
            
            // Create new colonies for non-largest components (min 5 cells to avoid tiny fragments)
            for (int c = 0; c < num_components; c++) {
                if (c == largest_idx) continue;
                if (sizes[c] < 5) {
                    // Tiny fragment - these cells will be orphaned (removed)
                    // Clear these cells from the grid
                    for (int j = 0; j < world->width * world->height; j++) {
                        if (world->cells[j].colony_id == colony->id &&
                            world->cells[j].component_id == c) {
                            world->cells[j].colony_id = 0;
                            world->cells[j].age = 0;
                            world->cells[j].is_border = false;
                        }
                    }
                    continue;
                }
                
                // Create new colony with mutated genome
                Colony new_colony;
                memset(&new_colony, 0, sizeof(Colony));
                new_colony.id = 0;
                generate_scientific_name(new_colony.name, sizeof(new_colony.name));
                new_colony.genome = colony->genome;
                genome_mutate(&new_colony.genome);
                new_colony.color = new_colony.genome.body_color;
                new_colony.cell_count = (size_t)sizes[c];
                new_colony.max_cell_count = (size_t)sizes[c];
                new_colony.active = true;
                new_colony.age = 0;
                new_colony.parent_id = colony->id;
                new_colony.hgt_plasmid_fraction = colony->hgt_plasmid_fraction;
                new_colony.hgt_is_transconjugant = colony->hgt_is_transconjugant;
                
                // Generate unique shape seed for procedural shape (inherit and mutate from parent)
                new_colony.shape_seed = colony->shape_seed ^ (uint32_t)rand() ^ ((uint32_t)rand() << 8);
                new_colony.wobble_phase = (float)(rand() % 628) / 100.0f;
                
                uint32_t new_id = world_add_colony(world, new_colony);
                
                // Update cells to belong to new colony
                for (int j = 0; j < world->width * world->height; j++) {
                    if (world->cells[j].colony_id == colony->id && 
                        world->cells[j].component_id == c) {
                        world->cells[j].colony_id = new_id;
                    }
                }
            }
            
            // Update original colony's cell count to largest component only
            colony->cell_count = (size_t)largest_size;
            division_occurred = true;  // Only one division per tick
        }
        
        free(sizes);
    }
}

void simulation_check_recombinations(World* world) {
    if (!world) return;
    
    // Check for adjacent compatible colonies
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony_a = world_get_colony(world, cell->colony_id);
            if (!colony_a) continue;
            
            // Check right and down neighbors only (avoid double-checking)
            for (int d = 1; d <= 2; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor || neighbor->colony_id == 0 || neighbor->colony_id == cell->colony_id) continue;
                
                Colony* colony_b = world_get_colony(world, neighbor->colony_id);
                if (!colony_b) continue;
                
                // Recombination only happens between very closely related colonies
                // (e.g., recently divided colonies that reconnect)
                // This requires checking parent_id relationship
                if (colony_a->parent_id != colony_b->id && colony_b->parent_id != colony_a->id) {
                    // Not parent-child, also check if siblings (same parent)
                    if (colony_a->parent_id == 0 || colony_a->parent_id != colony_b->parent_id) {
                        continue;  // Not related, no merge
                    }
                }
                
                // Calculate genetic distance - must be very close for siblings to merge
                float distance = genome_distance(&colony_a->genome, &colony_b->genome);
                
                // Very strict threshold - only nearly identical genomes merge
                float threshold = 0.05f;
                
                // Apply merge_affinity bonus: average of both colonies' affinities
                float avg_affinity = (colony_a->genome.merge_affinity + colony_b->genome.merge_affinity) / 2.0f;
                threshold += avg_affinity * 0.1f;  // Max bonus of 0.03
                
                // Check genetic compatibility with adjusted threshold
                if (distance <= threshold) {
                    // Merge: smaller colony joins larger
                    Colony* larger = colony_a->cell_count >= colony_b->cell_count ? colony_a : colony_b;
                    Colony* smaller = colony_a->cell_count >= colony_b->cell_count ? colony_b : colony_a;
                    
                    // Merge genomes
                    larger->genome = genome_merge(&larger->genome, larger->cell_count,
                                                  &smaller->genome, smaller->cell_count);
                    
                    // Transfer cells
                    for (int j = 0; j < world->width * world->height; j++) {
                        if (world->cells[j].colony_id == smaller->id) {
                            world->cells[j].colony_id = larger->id;
                        }
                    }
                    
                    larger->cell_count += smaller->cell_count;
                    smaller->cell_count = 0;
                    smaller->active = false;
                    
                    return;  // Only one merge per tick to keep things stable
                }
            }
        }
    }
}

// ============================================================================
// Parallel/Region-based processing functions
// ============================================================================

PendingBuffer* pending_buffer_create(int initial_capacity) {
    PendingBuffer* buf = (PendingBuffer*)malloc(sizeof(PendingBuffer));
    if (!buf) return NULL;
    
    buf->cells = (PendingCell*)malloc(initial_capacity * sizeof(PendingCell));
    if (!buf->cells) {
        free(buf);
        return NULL;
    }
    buf->count = 0;
    buf->capacity = initial_capacity;
    return buf;
}

void pending_buffer_destroy(PendingBuffer* buf) {
    if (buf) {
        free(buf->cells);
        free(buf);
    }
}

void pending_buffer_add(PendingBuffer* buf, int x, int y, uint32_t colony_id) {
    if (!buf) return;
    
    if (buf->count >= buf->capacity) {
        int new_capacity = buf->capacity * 2;
        PendingCell* new_cells = (PendingCell*)realloc(
            buf->cells, (size_t)new_capacity * sizeof(PendingCell)
        );
        if (!new_cells) return;
        buf->cells = new_cells;
        buf->capacity = new_capacity;
    }
    buf->cells[buf->count].x = x;
    buf->cells[buf->count].y = y;
    buf->cells[buf->count].colony_id = colony_id;
    buf->count++;
}

void pending_buffer_clear(PendingBuffer* buf) {
    if (buf) buf->count = 0;
}

// Horizontal gene transfer between touching colonies
// When two different colonies are adjacent, they can exchange genetic material
static void attempt_horizontal_gene_transfer(World* world, Colony* donor, Colony* recipient) {
    if (!world || !donor || !recipient) return;

    float donor_plasmid = utils_clamp_f(donor->hgt_plasmid_fraction, 0.0f, 1.0f);
    float recipient_plasmid = utils_clamp_f(recipient->hgt_plasmid_fraction, 0.0f, 1.0f);
    float recipient_pool = 1.0f - recipient_plasmid;
    if (donor_plasmid <= 0.0f || recipient_pool <= 0.0f) return;

    float donor_rate = donor->hgt_is_transconjugant
        ? world->hgt_kinetics.transconjugant_transfer_rate
        : world->hgt_kinetics.donor_transfer_rate;

    float contact_force = world->hgt_kinetics.contact_rate * donor->genome.gene_transfer_rate;
    float transfer_probability = contact_force * donor_rate * world->hgt_kinetics.recipient_uptake_rate *
                                 donor_plasmid * recipient_pool;
    transfer_probability = utils_clamp_f(transfer_probability, 0.0f, 1.0f);
    if (rand_float() >= transfer_probability) return;

    float gain = world->hgt_kinetics.transfer_efficiency * donor_plasmid * recipient_pool;
    float next_plasmid = utils_clamp_f(recipient_plasmid + gain, 0.0f, 1.0f);
    if (next_plasmid <= recipient_plasmid) return;

    bool became_transconjugant = recipient_plasmid <= 0.0f && next_plasmid > 0.0f;

    recipient->hgt_plasmid_fraction = next_plasmid;
    recipient->hgt_is_transconjugant = recipient->hgt_is_transconjugant || became_transconjugant;
    recipient->hgt_transfer_events_in++;
    donor->hgt_transfer_events_out++;
    world->hgt_metrics.transfer_events_total++;

    if (became_transconjugant) {
        world->hgt_metrics.transconjugant_events_total++;
    }

    if (!colonies_share_lineage(world, donor, recipient)) {
        world->hgt_metrics.cross_lineage_transfer_events_total++;
    }
}

void simulation_spread_region(World* world, int start_x, int start_y, 
                              int end_x, int end_y, PendingBuffer* pending) {
    if (!world || !pending) return;
    
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony) continue;
            
            // Try to spread to neighbors based on spread_rate
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor) continue;
                
                if (neighbor->colony_id == 0) {
                    // Empty cell - can spread (very aggressive)
                    float biomass_pressure = calculate_biomass_pressure(world, x, y, cell->colony_id);
                    float quorum_activation = get_quorum_activation(colony);
                    float quorum_boost = 1.0f + quorum_activation * colony->genome.motility * 0.8f;
                    float dormancy_factor = colony_spread_activity_factor(colony);
                    float trait_growth_cost = calculate_growth_cost_multiplier(colony);
                    float growth_uptake = monod_growth_multiplier(world, world->nutrients[y * world->width + x]);
                    float spread_chance = colony->genome.spread_rate * colony->genome.metabolism *
                                          biomass_pressure * quorum_boost * dormancy_factor *
                                          trait_growth_cost * growth_uptake *
                                          colony->hgt_fitness_scale * 3.2f;
                    if (rand_float() < spread_chance) {
                        pending_buffer_add(pending, nx, ny, cell->colony_id);
                    }
                } else if (neighbor->colony_id != cell->colony_id) {
                    // Enemy cell - aggressive takeover
                    Colony* enemy = world_get_colony(world, neighbor->colony_id);
                    if (enemy && enemy->active) {
                        // HORIZONTAL GENE TRANSFER: Small chance to exchange genes on contact
                        // This simulates conjugation, transformation, transduction
                        attempt_horizontal_gene_transfer(world, colony, enemy);
                        attempt_horizontal_gene_transfer(world, enemy, colony);
                        
                        // Wide variance in combat: aggression 0-1, defense 0-1
                        float attack = colony->genome.aggression * (1.0f + colony->genome.toxin_production * 0.8f);
                        float defense = enemy->genome.resilience * (0.3f + enemy->genome.defense_priority * 0.7f);
                        attack *= (1.0f + get_quorum_activation(colony) * 0.5f);
                        defense *= (1.0f + get_quorum_activation(enemy) * 0.3f);
                        float combat_chance = attack / (attack + defense + 0.05f);
                        
                        // Attack if random check passes (25% base + 50% from combat_chance)
                        if (rand_float() < 0.25f + combat_chance * 0.5f) {
                            pending_buffer_add(pending, nx, ny, cell->colony_id);
                        } else {
                            // DEFENSE REWARD: Defender successfully repelled attack
                            enemy->genome.resilience = utils_clamp_f(
                                enemy->genome.resilience + 0.003f, 0.0f, 1.0f);
                            enemy->genome.defense_priority = utils_clamp_f(
                                enemy->genome.defense_priority + 0.002f, 0.0f, 1.0f);
                        }
                    }
                }
            }
        }
    }
}

void simulation_apply_pending(World* world, PendingBuffer** buffers, int buffer_count) {
    if (!world || !buffers) return;
    
    for (int b = 0; b < buffer_count; b++) {
        PendingBuffer* pending = buffers[b];
        if (!pending) continue;
        
        for (int i = 0; i < pending->count; i++) {
            Cell* cell = world_get_cell(world, pending->cells[i].x, pending->cells[i].y);
            if (cell) {
                uint32_t old_colony_id = cell->colony_id;
                uint32_t new_colony_id = pending->cells[i].colony_id;
                Colony* new_colony = world_get_colony(world, new_colony_id);
                
                // Update old colony's cell count
                if (old_colony_id != 0) {
                    Colony* old = world_get_colony(world, old_colony_id);
                    if (old && old->cell_count > 0) {
                        old->cell_count--;
                        
                        // COMBAT REWARD: Conquering colony gains resources from enemy
                        if (new_colony && old_colony_id != new_colony_id) {
                            // Attacker gets a metabolism boost from consuming enemy
                            float resource_gain = old->genome.resource_consumption * 0.02f;
                            new_colony->genome.metabolism = utils_clamp_f(
                                new_colony->genome.metabolism + resource_gain, 0.0f, 1.0f);
                            
                            // Small aggression boost for successful attacks
                            new_colony->genome.aggression = utils_clamp_f(
                                new_colony->genome.aggression + 0.005f, 0.0f, 1.0f);
                            
                            // Track success in neural network
                            int dx = pending->cells[i].x - (world->width / 2);
                            int dy = pending->cells[i].y - (world->height / 2);
                            int dir = (dx > 0 ? 1 : 0) + (dy > 0 ? 2 : 0);  // Simple directional encoding
                            new_colony->success_history[dir % 8] += 0.1f;
                        }
                    }
                }
                
                // Colonize
                cell->colony_id = new_colony_id;
                cell->age = 0;
                
                // Update new colony's cell count
                if (new_colony) {
                    new_colony->cell_count++;
                }
            }
        }
    }
}

void simulation_mutate_region(World* world, int start_x, int start_y, 
                              int end_x, int end_y) {
    (void)start_x; (void)start_y; (void)end_x; (void)end_y;
    // Mutations happen per-colony, not per-cell, so region doesn't matter
    // This is kept for API consistency; actual mutation happens in simulation_mutate
    if (!world) return;
}

void simulation_age_region(World* world, int start_x, int start_y, 
                           int end_x, int end_y) {
    if (!world) return;
    
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            int idx = y * world->width + x;
            if (world->cells[idx].colony_id != 0 && world->cells[idx].age < 255) {
                world->cells[idx].age++;
            }
        }
    }
}

void simulation_update_colony_stats(World* world) {
    if (!world) return;

    const int width = world->width;
    const int height = world->height;
    Cell* cells = world->cells;

    // First recount all cells from grid to ensure accuracy
    for (size_t i = 0; i < world->colony_count; i++) {
        world->colonies[i].cell_count = 0;
    }

    for (int y = 0; y < height; y++) {
        int row = y * width;
        for (int x = 0; x < width; x++) {
            int idx = row + x;
            Cell* cell = &cells[idx];
            uint32_t cid = cell->colony_id;
            if (cid == 0) {
                cell->is_border = false;
                continue;
            }

            Colony* colony = lookup_active_colony(world, cid);
            if (colony) {
                colony->cell_count++;
            }

            bool border = false;
            if (y > 0 && cells[idx - width].colony_id != cid) {
                border = true;
            } else if (x + 1 < width && cells[idx + 1].colony_id != cid) {
                border = true;
            } else if (y + 1 < height && cells[idx + width].colony_id != cid) {
                border = true;
            } else if (x > 0 && cells[idx - 1].colony_id != cid) {
                border = true;
            }
            cell->is_border = border;
        }
    }

    // Now update stats
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        
        // Track max population
        if (colony->cell_count > colony->max_cell_count) {
            colony->max_cell_count = colony->cell_count;
        }
        
        // Mark as dead if population hits 0
        if (colony->cell_count == 0) {
            colony->active = false;
            continue;
        }
        
        // Animate wobble phase for organic movement
        colony->wobble_phase += 0.03f;
        if (colony->wobble_phase > 6.28318f) colony->wobble_phase -= 6.28318f;
        
        // Note: shape_seed is NOT mutated - that causes jarring visual jumps
        // Shape evolution happens naturally through smooth wobble_phase animation
    }
}

// ============================================================================
// Environmental Dynamics Functions
// ============================================================================

// Update nutrient levels: deplete where cells are, regenerate elsewhere
void simulation_update_nutrients(World* world) {
    if (!world || !world->nutrients) return;
    
    int total_cells = world->width * world->height;
    Cell* cells = world->cells;
    float* nutrients = world->nutrients;
    
    for (int i = 0; i < total_cells; i++) {
        uint32_t colony_id = cells[i].colony_id;
        if (colony_id != 0) {
            // Cells consume nutrients based on metabolism
            Colony* colony = lookup_active_colony(world, colony_id);
            float consumption = NUTRIENT_DEPLETION_RATE;
            if (colony) {
                consumption *= colony->genome.metabolism;
                consumption *= (1.0f - colony->genome.efficiency * 0.5f);
                consumption *= (1.0f + calculate_expensive_trait_load(&colony->genome) * 0.6f);
                consumption *= monod_uptake_multiplier(world, nutrients[i]);
            }
            float value = nutrients[i] - consumption;
            if (value < 0.0f) value = 0.0f;
            else if (value > 1.0f) value = 1.0f;
            nutrients[i] = value;
        } else {
            // Empty cells slowly regenerate nutrients
            float value = nutrients[i] + NUTRIENT_REGEN_RATE;
            if (value < 0.0f) value = 0.0f;
            else if (value > 1.0f) value = 1.0f;
            nutrients[i] = value;
        }
    }

    diffuse_scalar_field(world,
                         world->nutrients,
                         world->scratch_nutrients,
                         g_transport_params.nutrient_diffusivity);
}

// Decay toxins over time
void simulation_decay_toxins(World* world) {
    if (!world || !world->toxins) return;

    apply_diffusion_decay(world, world->toxins, &world->rd_controls.toxins, true);
}

// ============================================================================
// Competitive Strategy Functions
// ============================================================================

// Produce toxins around colony borders
void simulation_produce_toxins(World* world) {
    if (!world || !world->toxins) return;

    // Decay/diffuse existing toxins before new emissions.
    simulation_decay_toxins(world);
    
    // Each border cell produces toxins
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            // Border cells produce toxins based on toxin_production trait
            // High defense_priority increases toxin output at borders
            float production = colony->genome.toxin_production * 
                               (1.0f + colony->genome.defense_priority * 0.5f);
            
            // Emit toxins to neighboring cells
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                if (nx < 0 || nx >= world->width || ny < 0 || ny >= world->height) continue;
                
                int idx = ny * world->width + nx;
                world->toxins[idx] = utils_clamp_f(world->toxins[idx] + production * 0.1f, 0.0f, 1.0f);
            }
        }
    }
}

// Apply toxin damage to cells
void simulation_apply_toxin_damage(World* world) {
    if (!world || !world->toxins) return;
    
    // Collect cells that should die from toxins
    typedef struct { int x, y; } DeadCell;
    DeadCell* dead = NULL;
    int dead_count = 0;
    int dead_capacity = 64;
    dead = (DeadCell*)malloc(dead_capacity * sizeof(DeadCell));
    if (!dead) return;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int idx = y * world->width + x;
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            float toxin_level = world->toxins[idx];
            if (toxin_level <= 0.01f) continue;
            
            // Damage = toxin_level * (1 - resistance) * vulnerability_factor
            // High defense_priority reduces damage at borders
            float vulnerability = cell->is_border ? 
                (1.0f - colony->genome.defense_priority * 0.3f) : 1.0f;
            float damage = toxin_level * (1.0f - colony->genome.toxin_resistance) * vulnerability;
            
            // Probabilistic death based on damage
            if (rand_float() < damage * 0.15f) {
                if (dead_count >= dead_capacity) {
                    dead_capacity *= 2;
                    DeadCell* new_dead = (DeadCell*)realloc(dead, dead_capacity * sizeof(DeadCell));
                    if (!new_dead) {
                        free(dead);
                        return;
                    }
                    dead = new_dead;
                }
                dead[dead_count++] = (DeadCell){x, y};
            }
        }
    }
    
    // Kill cells
    for (int i = 0; i < dead_count; i++) {
        Cell* cell = world_get_cell(world, dead[i].x, dead[i].y);
        if (cell && cell->colony_id != 0) {
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (colony && colony->cell_count > 0) {
                colony->cell_count--;
            }
            cell->colony_id = 0;
            cell->age = 0;
            cell->is_border = false;
        }
    }
    
    free(dead);
}

// Handle resource consumption and nutrient depletion
void simulation_consume_resources(World* world) {
    if (!world || !world->nutrients) return;
    
    int total = world->width * world->height;
    
    // Single fused pass: consumption + regeneration + toxin decay
    for (int i = 0; i < total; i++) {
        uint32_t cid = world->cells[i].colony_id;
        float n = world->nutrients[i];
        
        if (cid != 0) {
            Colony* colony = world_get_colony(world, cid);
            if (colony && colony->active) {
                float consumption = colony->genome.resource_consumption * 
                                    (0.5f + colony->genome.aggression * 0.5f);
                consumption *= (1.0f + calculate_expensive_trait_load(&colony->genome) * 0.6f);
                consumption *= monod_uptake_multiplier(world, n);
                n -= consumption * 0.05f;
            }
            // Occupied cells regenerate very slowly
            n += 0.002f;
        } else {
            // Empty cells regenerate quickly
            n += 0.02f;
        }
        
        // Clamp nutrients
        if (n < 0.0f) n = 0.0f;
        else if (n > 1.0f) n = 1.0f;
        world->nutrients[i] = n;
    }
    
    simulation_decay_toxins(world);
}

// ============================================================================
// Scent/Signal System - Colonies emit scent that diffuses outward
// ============================================================================

// Emit and diffuse colony scents - colonies can detect each other at distance
void simulation_update_scents(World* world) {
    if (!world || !world->signals || !world->signal_source) return;
    
    const int width = world->width;
    const int height = world->height;
    int total = world->width * world->height;
    Cell* cells = world->cells;
    float* signals = world->signals;
    uint32_t* signal_source = world->signal_source;
    
    // Use pre-allocated scratch buffers instead of per-tick calloc
    float* new_signals = world->scratch_signals;
    uint32_t* new_sources = world->scratch_sources;
    if (!new_signals || !new_sources) return;
    
    memset(new_signals, 0, total * sizeof(float));
    memset(new_sources, 0, total * sizeof(uint32_t));
    
    // Step 1: Emit scent from colony cells (stronger at borders)
    for (int idx = 0; idx < total; idx++) {
            Cell* cell = &cells[idx];
            if (cell->colony_id == 0) continue;
            
            Colony* colony = lookup_active_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            // Emit scent based on signal_emission trait
            float emission = colony->genome.signal_emission * 0.3f;
            if (cell->is_border) emission *= 2.0f;
            emission *= (1.0f + (float)colony->cell_count / 500.0f);
            
            new_signals[idx] += emission;
            new_sources[idx] = cell->colony_id;
    }
    
    float signal_decay = world->rd_controls.signals.decay;
    float signal_diffusion = world->rd_controls.signals.diffusion;
    if (g_transport_params_overridden) {
        signal_decay = g_transport_params.signal_decay;
        signal_diffusion = g_transport_params.signal_neighbor_transfer;
    }
    bool has_eps_barrier = world_has_eps_barrier(world);

    // Step 2: Diffuse existing scent with EPS-dependent effective diffusivity
    for (int y = 0; y < height; y++) {
        int row = y * width;
        for (int x = 0; x < width; x++) {
            int idx = row + x;
            float current = signals[idx];
            if (current < 0.001f) continue;
            
            uint32_t source = signal_source[idx];
            float center_eps = has_eps_barrier ? get_cell_eps_density(world, idx) : 0.0f;
            float retained = current * (1.0f - signal_decay);
            float remaining = retained;

            int neighbor_idx[4];
            float neighbor_weight[4];
            int neighbor_count = 0;
            if (y > 0) {
                int ni = idx - width;
                neighbor_idx[neighbor_count] = ni;
                neighbor_weight[neighbor_count++] = has_eps_barrier
                    ? signal_diffusion * effective_diffusivity_scale(center_eps, get_cell_eps_density(world, ni))
                    : signal_diffusion;
            }
            if (x + 1 < width) {
                int ni = idx + 1;
                neighbor_idx[neighbor_count] = ni;
                neighbor_weight[neighbor_count++] = has_eps_barrier
                    ? signal_diffusion * effective_diffusivity_scale(center_eps, get_cell_eps_density(world, ni))
                    : signal_diffusion;
            }
            if (y + 1 < height) {
                int ni = idx + width;
                neighbor_idx[neighbor_count] = ni;
                neighbor_weight[neighbor_count++] = has_eps_barrier
                    ? signal_diffusion * effective_diffusivity_scale(center_eps, get_cell_eps_density(world, ni))
                    : signal_diffusion;
            }
            if (x > 0) {
                int ni = idx - 1;
                neighbor_idx[neighbor_count] = ni;
                neighbor_weight[neighbor_count++] = has_eps_barrier
                    ? signal_diffusion * effective_diffusivity_scale(center_eps, get_cell_eps_density(world, ni))
                    : signal_diffusion;
            }

            if (neighbor_count > 0) {
                float weight_sum = 0.0f;
                for (int n = 0; n < neighbor_count; n++) {
                    if (neighbor_weight[n] > 0.0f) {
                        weight_sum += neighbor_weight[n];
                    }
                }

                if (weight_sum > 0.0f) {
                    float max_neighbor_fraction = (1.0f - signal_decay);
                    float transfer_scale = 1.0f;
                    if (weight_sum > max_neighbor_fraction) {
                        transfer_scale = max_neighbor_fraction / weight_sum;
                    }

                    float spent = 0.0f;
                    for (int n = 0; n < neighbor_count; n++) {
                        float weight = neighbor_weight[n];
                        if (weight <= 0.0f) continue;

                        float transfer = current * weight * transfer_scale;
                        if (transfer <= 0.0f) continue;

                        int ni = neighbor_idx[n];
                        float prev = new_signals[ni];
                        new_signals[ni] = prev + transfer;
                        spent += transfer;
                        if (source > 0 && transfer > prev) {
                            new_sources[ni] = source;
                        }
                    }
                    remaining = retained - spent;
                    if (remaining < 0.0f) remaining = 0.0f;
                }
            }

            if (remaining > 0.0f) {
                float prev = new_signals[idx];
                new_signals[idx] = prev + remaining;
                if (source > 0 && (new_sources[idx] == 0 || remaining > prev)) {
                    new_sources[idx] = source;
                }
            }
        }
    }
    
    // Clamp and copy back
    simd_clamp01_copy(world->signals, new_signals, total);
    memcpy(world->signal_source, new_sources, total * sizeof(uint32_t));
}

// Get scent influence on spread direction - returns modifier for given direction
static float get_scent_influence(World* world, int x, int y, int dx, int dy, 
                                  uint32_t colony_id, const Genome* genome) {
    int nx = x + dx, ny = y + dy;
    if (nx < 0 || nx >= world->width || ny < 0 || ny >= world->height) {
        return 1.0f;
    }
    
    int idx = ny * world->width + nx;
    float scent = world->signals[idx];
    uint32_t source = world->signal_source[idx];
    
    if (scent < 0.01f || source == 0) return 1.0f;
    
    // Scent from self/allies - might attract (social) or repel (aggressive expansion)
    if (source == colony_id) {
        // Self-scent: avoid overcrowding unless high density tolerance
        return 1.0f - scent * (1.0f - genome->density_tolerance) * 0.3f;
    }
    
    // Enemy scent - react based on aggression vs defense
    float aggression = genome->aggression;
    float defense = genome->defense_priority;
    
    // High aggression: attracted to enemy scent (hunt them)
    // High defense: repelled by enemy scent (avoid conflict)
    float reaction = (aggression - defense) * genome->signal_sensitivity;
    
    // reaction > 0: attracted (boost spread toward enemy)
    // reaction < 0: repelled (reduce spread toward enemy)
    return 1.0f + reaction * scent * 0.5f;
}

// Combat resolution when colonies meet at borders
void simulation_resolve_combat(World* world) {
    if (!world) return;

    const int width = world->width;
    const int height = world->height;
    Cell* cells = world->cells;
    uint32_t* combat_cache = world->scratch_sources;
    const uint32_t cache_tick = combat_cache_tick_tag(world->tick);
    
    // Decay/diffuse existing toxins.
    if (world->toxins) {
        simulation_decay_toxins(world);
    }
    
    typedef struct { int x, y; uint32_t winner; uint32_t loser; float strength; } CombatResult;
    CombatResult* results = NULL;
    ContestedBorder* contested = NULL;
    int contested_count = 0;
    int contested_capacity = 0;
    int result_count = 0;
    int result_capacity = 128;
    results = (CombatResult*)malloc(result_capacity * sizeof(CombatResult));
    if (!results) return;
    
    // First pass: emit toxins from aggressive colonies to create hostile zones
    for (int y = 0; y < height; y++) {
        int row = y * width;
        for (int x = 0; x < width; x++) {
            int idx = row + x;
            Cell* cell = &cells[idx];
            if (cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* colony = lookup_active_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;

            uint8_t enemy_mask = detect_enemy_neighbor_mask(cells, width, height, x, y, cell->colony_id);
            int friendly_count = 0;
            if (enemy_mask != 0) {
                friendly_count = count_friendly_neighbors(world, x, y, cell->colony_id);
                if (!append_contested_border(&contested,
                                             &contested_count,
                                             &contested_capacity,
                                             idx,
                                             enemy_mask,
                                             friendly_count)) {
                    free(contested);
                    free(results);
                    return;
                }
            }

            if (combat_cache) {
                combat_cache[idx] = pack_combat_cache(cache_tick, friendly_count, enemy_mask);
            }
            
            // Border cells emit toxins based on toxin_production and quorum activation
            float quorum_activation = get_quorum_activation(colony);
            float toxin_emit = colony->genome.toxin_production *
                               (0.06f + 0.06f * quorum_activation);
            toxin_emit *= colony_toxin_output_factor(colony);
            if (toxin_emit > 0.01f) {
                // Emit to self and neighbors
                world->toxins[idx] = utils_clamp_f(world->toxins[idx] + toxin_emit, 0.0f, 1.0f);
                if (y > 0) {
                    int ni = idx - width;
                    world->toxins[ni] = utils_clamp_f(world->toxins[ni] + toxin_emit * 0.5f, 0.0f, 1.0f);
                }
                if (x + 1 < width) {
                    int ni = idx + 1;
                    world->toxins[ni] = utils_clamp_f(world->toxins[ni] + toxin_emit * 0.5f, 0.0f, 1.0f);
                }
                if (y + 1 < height) {
                    int ni = idx + width;
                    world->toxins[ni] = utils_clamp_f(world->toxins[ni] + toxin_emit * 0.5f, 0.0f, 1.0f);
                }
                if (x > 0) {
                    int ni = idx - 1;
                    world->toxins[ni] = utils_clamp_f(world->toxins[ni] + toxin_emit * 0.5f, 0.0f, 1.0f);
                }
            }
        }
    }

    diffuse_scalar_field(world,
                         world->toxins,
                         world->scratch_toxins,
                         g_transport_params.toxin_diffusivity);
    
    // Second pass: resolve combat at borders with strategic modifiers
    for (int i = 0; i < contested_count; i++) {
        int idx = contested[i].idx;
        int x = idx % width;
        int y = idx / width;
        Cell* cell = &cells[idx];
        if (cell->colony_id == 0) continue;

        Colony* attacker = lookup_active_colony(world, cell->colony_id);
        if (!attacker || !attacker->active) continue;

        // Skip if colony is in retreat mode (stressed and defensive)
        if (attacker->stress_level > 0.7f && attacker->genome.defense_priority > 0.6f) {
            continue;
        }

        const int attacker_friendly = (int)contested[i].friendly_count;
        const float flanking_bonus = 1.0f + (attacker_friendly * 0.15f);
        const float attacker_base = attacker->genome.aggression * 1.2f;
        const float attacker_toxin_bonus = attacker->genome.toxin_production * 0.4f;
        const float attacker_nutrient_factor = world->nutrients
            ? (0.6f + world->nutrients[idx] * 0.5f)
            : 1.0f;
        const float attacker_toxin_factor = world->nutrients
            ? (1.0f - world->toxins[idx] * (1.0f - attacker->genome.toxin_resistance))
            : 1.0f;
        const float attacker_stress_factor = (attacker->stress_level > 0.5f)
            ? (1.0f + attacker->genome.aggression * 0.3f)
            : 1.0f;

        for (int d = 0; d < 4; d++) {
            if ((contested[i].enemy_mask & (1u << d)) == 0) continue;

            int nx = x + DX[d];
            int ny = y + DY[d];
            int nidx = ny * width + nx;

            Cell* neighbor = &cells[nidx];
            if (neighbor->colony_id == 0 ||
                neighbor->colony_id == cell->colony_id) continue;

            Colony* defender = lookup_active_colony(world, neighbor->colony_id);
            if (!defender || !defender->active) continue;

            int defender_friendly = -1;
            if (combat_cache) {
                defender_friendly = combat_cached_friendly_count(combat_cache[nidx], cache_tick);
            }
            if (defender_friendly < 0) {
                defender_friendly = count_friendly_neighbors(world, nx, ny, neighbor->colony_id);
            }

            float attack_str = attacker_base * flanking_bonus;
            float defend_str = defender->genome.resilience;

            // 2. DEFENSIVE FORMATION: Defenders with high defense_priority are harder to crack
            float defensive_bonus = 1.0f + (defender->genome.defense_priority * defender_friendly * 0.2f);
            defend_str *= defensive_bonus;

            // 3. DIRECTIONAL PREFERENCE: Colonies fight harder in preferred directions
            float dir_weight = attacker->genome.spread_weights[CARDINAL_DIR8_INDEX[d]];
            attack_str *= (0.7f + dir_weight * 0.6f);

            // 4. TOXIN WARFARE: Toxin production aids attack, resistance aids defense
            attack_str += attacker_toxin_bonus;
            defend_str += defender->genome.toxin_resistance * 0.3f;

            // 5. BIOFILM DEFENSE: Defenders with biofilm are harder to defeat
            defend_str *= (1.0f + defender->biofilm_strength * 0.3f);

            // 6. NUTRIENT ADVANTAGE: Well-fed cells fight better
            if (world->nutrients) {
                defend_str *= (0.6f + world->nutrients[nidx] * 0.5f);
                defend_str *= (1.0f - world->toxins[nidx] * (1.0f - defender->genome.toxin_resistance));
            }
            attack_str *= attacker_nutrient_factor * attacker_toxin_factor;

            // 7. MOMENTUM: Colonies that have been winning keep winning (success_history)
            attack_str *= (0.8f + attacker->success_history[d] * 0.4f);
            attack_str *= attacker_stress_factor;

            // 8. STRESSED COLONIES FIGHT DIFFERENTLY
            if (defender->stress_level > 0.5f && defender->genome.defense_priority < 0.4f) {
                // Stressed non-defensive colonies crumble
                defend_str *= 0.7f;
            }

            // 9. SIZE MATTERS: Larger colonies have morale advantage
            float size_ratio = (float)attacker->cell_count / (float)(defender->cell_count + 1);
            if (size_ratio > 2.0f) attack_str *= 1.15f;  // 2x larger = bonus
            if (size_ratio < 0.5f) attack_str *= 0.85f;  // 2x smaller = penalty

            // === COMBAT RESOLUTION ===
            float attack_chance = attack_str / (attack_str + defend_str + 0.1f);

            // Per-cell spatial noise for ragged borders (prevents straight Voronoi lines)
            float combat_noise = 0.5f + rand_float() * 1.0f;  // 0.5-1.5x

            if (rand_float() < attack_chance * combat_noise) {
                // Attacker wins - record result
                if (result_count >= result_capacity) {
                    result_capacity *= 2;
                    CombatResult* new_results = (CombatResult*)realloc(results, result_capacity * sizeof(CombatResult));
                    if (!new_results) {
                        free(contested);
                        free(results);
                        return;
                    }
                    results = new_results;
                }
                results[result_count++] = (CombatResult){nx, ny, cell->colony_id, neighbor->colony_id, attack_str};

                // Update success history for learning
                attacker->success_history[d] = utils_clamp_f(
                    attacker->success_history[d] + 0.05f * attacker->genome.learning_rate, 0.0f, 1.0f);
            } else if (rand_float() < 0.3f) {
                // Defender successfully defends - slight penalty to attacker's direction
                attacker->success_history[d] = utils_clamp_f(
                    attacker->success_history[d] - 0.02f * attacker->genome.learning_rate, 0.0f, 1.0f);
            }
        }
    }
    
    // Apply combat results
    for (int i = 0; i < result_count; i++) {
        int idx = results[i].y * width + results[i].x;
        Cell* cell = &cells[idx];
        if (cell && cell->colony_id == results[i].loser) {
            Colony* loser = lookup_active_colony(world, results[i].loser);
            Colony* winner = lookup_active_colony(world, results[i].winner);
            
            if (loser && loser->cell_count > 0) {
                loser->cell_count--;
                // Increase stress when losing cells
                loser->stress_level = utils_clamp_f(loser->stress_level + 0.01f, 0.0f, 1.0f);
            }
            
            // Cell is captured by winner
            if (winner && winner->active) {
                cell->colony_id = results[i].winner;
                cell->age = 0;
                cell->is_border = true;
                winner->cell_count++;
                // Reduce stress when winning
                winner->stress_level = utils_clamp_f(winner->stress_level - 0.005f, 0.0f, 1.0f);
            } else {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
            }
        }
    }
    
    free(contested);
    free(results);
}

void simulation_tick(World* world) {
    if (!world) return;
    
    // Age all cells and handle starvation/toxin death
    for (int i = 0; i < world->width * world->height; i++) {
        Cell* cell = &world->cells[i];
        if (cell->colony_id == 0) continue;
        
        // Age the cell
        if (cell->age < 255) cell->age++;
        
        Colony* colony = world_get_colony(world, cell->colony_id);
        if (!colony || !colony->active) continue;
        float trait_survival_cost = calculate_survival_cost_multiplier(colony);
        
        // STARVATION: Cells in depleted areas may die
        float nutrients = world->nutrients[i];
        if (nutrients < 0.2f) {
            // Low nutrients - chance of cell death based on efficiency
            float death_chance = (0.2f - nutrients) * 0.1f *
                                 (1.0f - colony->genome.efficiency) * trait_survival_cost;
            if (rand_float() < death_chance) {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
                if (colony->cell_count > 0) colony->cell_count--;
                colony->stress_level = utils_clamp_f(colony->stress_level + 0.02f, 0.0f, 1.0f);
                continue;
            }
        }
        
        // TOXIN DEATH: Cells in toxic areas may die
        float toxins = world->toxins[i];
        if (toxins > 0.3f) {
            float death_chance = (toxins - 0.3f) * 0.15f *
                                 (1.0f - colony->genome.toxin_resistance) * trait_survival_cost;
            if (rand_float() < death_chance) {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
                if (colony->cell_count > 0) colony->cell_count--;
                colony->stress_level = utils_clamp_f(colony->stress_level + 0.02f, 0.0f, 1.0f);
                continue;
            }
        }
        
        // NATURAL DECAY: All cells have a small baseline death rate
        // Based on bacterial stationary-phase death rates (~0.1-0.3/hour)
        // Border cells die faster (exposed to environment)
        float base_death_rate = 0.006f;  // ~0.6% per tick (realistic range)
        if (cell->is_border) {
            base_death_rate = 0.014f;  // Border cells more vulnerable
        }
        
        // SIZE-BASED DECAY: Larger colonies decay slightly faster
        // Resources must travel from border to interior
        float size_factor = 1.0f;
        if (colony->cell_count > 100) {
            size_factor = 1.0f + (float)(colony->cell_count - 100) / 1000.0f;
        }
        base_death_rate *= size_factor;
        
        // DEATH ZONE (nutrient starvation in colony interior)
        // Interior cells that aren't at the border starve slightly faster
        if (!cell->is_border && colony->cell_count > 60) {
            float interior_penalty = 1.0f + (float)colony->cell_count / 400.0f;
            interior_penalty = utils_clamp_f(interior_penalty, 1.0f, 2.0f);
            base_death_rate *= interior_penalty;
        }
        
        // Biofilm protects against natural decay
        base_death_rate *= (1.0f - colony->biofilm_strength * 0.5f);
        // Efficiency reduces decay (better resource management)
        base_death_rate *= (1.0f - colony->genome.efficiency * 0.4f);
        // Persister-like dormancy reduces death but suppresses growth elsewhere
        base_death_rate *= trait_survival_cost;
        base_death_rate *= colony_turnover_factor(colony);
        
        if (rand_float() < base_death_rate) {
            // NUTRIENT RECYCLING: Dead cells release nutrients back into environment
            // This helps sustain colony growth at the edges (based on research)
            int idx = i;
            if (world->nutrients) {
                // Dead cells release 30-50% of their "mass" as nutrients
                float nutrient_release = 0.3f + rand_float() * 0.2f;
                world->nutrients[idx] = utils_clamp_f(
                    world->nutrients[idx] + nutrient_release, 0.0f, 1.0f);
            }
            
            cell->colony_id = 0;
            cell->age = 0;
            cell->is_border = false;
            if (colony->cell_count > 0) colony->cell_count--;
            continue;
        }
        
        // OLD AGE: Cells have limited lifespan
        if (cell->age > 120) {
            float age_death_chance = (cell->age - 120) * 0.001f;
            if (rand_float() < age_death_chance) {
                // Nutrient recycling for old age death too
                int idx = i;
                if (world->nutrients) {
                    world->nutrients[idx] = utils_clamp_f(
                        world->nutrients[idx] + 0.25f, 0.0f, 1.0f);
                }
                
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
                if (colony->cell_count > 0) colony->cell_count--;
            }
        }
    }
    
    // Update environmental layers
    simulation_update_nutrients(world);
    
    // ENVIRONMENTAL DISTURBANCES: Periodic nutrient/toxin fluctuations
    // Every ~20 ticks, create environmental events
    if (world->tick % 20 == 0) {
        // 50% chance: nutrient shift
        if (rand_float() < 0.5f) {
            int cx = rand() % world->width;
            int cy = rand() % world->height;
            int radius = 10 + rand() % 20;
            for (int y = cy - radius; y <= cy + radius; y++) {
                for (int x = cx - radius; x <= cx + radius; x++) {
                    if (x >= 0 && x < world->width && y >= 0 && y < world->height) {
                        int dist2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                        if (dist2 <= radius * radius) {
                            int idx = y * world->width + x;
                            // Random nutrient change
                            world->nutrients[idx] = utils_clamp_f(
                                world->nutrients[idx] + (rand_float() - 0.5f) * 0.4f, 0.0f, 1.0f);
                        }
                    }
                }
            }
        }
    }
    
    // Run simulation phases
    simulation_update_hgt_kinetics(world);
    simulation_spread(world);
    simulation_mutate(world);
    // Division detection is expensive (flood-fill per colony) — only check every 10 ticks
    if (world->tick % 10 == 0) {
        simulation_check_divisions(world);
    }
    // Recombination is expensive (full grid scan) — only check every 15 ticks
    if (world->tick % 15 == 5) {
        simulation_check_recombinations(world);
    }
    
    // Combat resolution for more dynamic battles
    simulation_resolve_combat(world);
    
    // Count active colonies and total cells
    int active_colonies = 0;
    int total_cells = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) {
            active_colonies++;
            total_cells += world->colonies[i].cell_count;
        }
    }
    
    // Spontaneous generation: constant influx of new colonies
    // Creates competition and prevents any colony from dominating forever
    int world_size = world->width * world->height;
    float empty_ratio = 1.0f - (float)total_cells / (float)world_size;
    
    // Always spawn if population is too low (prevents simulation death)
    bool force_spawn = active_colonies < 3;
    
    // Base 5% spawn chance + more when empty space available + emergency spawns
    float spawn_chance = 0.05f + empty_ratio * 0.15f;
    if (active_colonies < 10) spawn_chance += 0.2f;  // Boost when few colonies
    
    if ((active_colonies < 200 && rand_float() < spawn_chance) || force_spawn) {
        // Find a random empty spot
        for (int attempts = 0; attempts < 50; attempts++) {
            int x = rand() % world->width;
            int y = rand() % world->height;
            Cell* cell = world_get_cell(world, x, y);
            if (cell && cell->colony_id == 0) {
                // Spawn a new random colony here
                Colony colony;
                memset(&colony, 0, sizeof(Colony));
                colony.genome = genome_create_random();
                colony.color = colony.genome.body_color;
                colony.cell_count = 1;
                colony.max_cell_count = 1;
                colony.active = true;
                colony.is_persister = false;
                colony.parent_id = 0;
                colony.hgt_plasmid_fraction = utils_clamp_f(colony.genome.gene_transfer_rate * 0.25f, 0.0f, 0.35f);
                colony.hgt_is_transconjugant = false;
                colony.shape_seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
                colony.wobble_phase = (float)(rand() % 628) / 100.0f;
                generate_scientific_name(colony.name, sizeof(colony.name));
                
                uint32_t id = world_add_colony(world, colony);
                if (id > 0) {
                    cell->colony_id = id;
                    cell->age = 0;
                }
                break;
            }
        }
    }
    
    // Recount all colony cell counts from grid to fix any inconsistencies
    simulation_update_colony_stats(world);
    
    // Update colony stats, state, and strategy
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        
        // Track max population
        if (colony->cell_count > colony->max_cell_count) {
            colony->max_cell_count = colony->cell_count;
        }
        
        // Mark as dead if population hits 0
        if (colony->cell_count == 0) {
            colony->active = false;
            continue;
        }
        
        // === QUORUM SENSING: autoinducer accumulation + decay ===
        // Use dynamic signal_strength as colony memory instead of mutating genome baselines
        float colony_density = (float)colony->cell_count / (float)(world->width * world->height);
        float scaled_density = utils_clamp_f(colony_density * 900.0f, 0.0f, 1.0f);
        float ai_input = scaled_density * colony->genome.signal_emission *
                         (0.7f + colony->genome.signal_sensitivity * 0.6f);
        ai_input *= colony_signal_activity_factor(colony);
        colony->signal_strength = utils_clamp_f(
            colony->signal_strength * 0.92f + ai_input * 0.35f, 0.0f, 1.0f
        );

        float quorum_activation = get_quorum_activation(colony);
        if (quorum_activation > 0.0f && colony->genome.biofilm_tendency > 0.3f) {
            // Quorum encourages collective biofilm protection
            colony->biofilm_strength = utils_clamp_f(
                colony->biofilm_strength + 0.002f + quorum_activation * 0.004f,
                0.0f, 1.0f
            );
        }
        
        // === STRATEGIC STATE UPDATES ===
        
        // Stress decay over time (colonies recover)
        colony->stress_level = utils_clamp_f(colony->stress_level - 0.002f, 0.0f, 1.0f);
        
        // Biofilm builds up based on investment trait
        if (colony->genome.biofilm_investment > 0.3f) {
            float target_biofilm = colony->genome.biofilm_investment * colony->genome.biofilm_tendency;
            if (colony->biofilm_strength < target_biofilm) {
                colony->biofilm_strength = utils_clamp_f(colony->biofilm_strength + 0.01f, 0.0f, 1.0f);
            }
        }
        // Biofilm decays slightly without investment
        colony->biofilm_strength = utils_clamp_f(colony->biofilm_strength - 0.002f, 0.0f, 1.0f);
        
        // Success history decay (fade old learnings)
        for (int d = 0; d < 8; d++) {
            colony->success_history[d] *= (0.995f + colony->genome.memory_factor * 0.004f);
        }
        
        // Update colony state based on stress
        if (colony->stress_level > colony->genome.sporulation_threshold && colony->genome.dormancy_threshold > 0.3f) {
            colony->state = COLONY_STATE_DORMANT;
            colony->is_dormant = true;
        } else if (colony->stress_level > 0.5f) {
            colony->state = COLONY_STATE_STRESSED;
            colony->is_dormant = false;
        } else {
            colony->state = COLONY_STATE_NORMAL;
            colony->is_dormant = false;
        }
        colony_update_persister_switching(colony);
        
        // Track population changes for learning
        int pop_delta = (int)colony->cell_count - (int)colony->last_population;
        colony->last_population = (uint32_t)colony->cell_count;
        
        // If growing, reinforce current strategy; if shrinking, consider changing
        if (pop_delta < -3 && colony->genome.learning_rate > 0.3f) {
            // Losing cells - slightly randomize success history to try new strategies
            int random_dir = rand() % 8;
            colony->success_history[random_dir] = utils_clamp_f(
                colony->success_history[random_dir] + 0.1f * rand_float(), 0.0f, 1.0f);
        }
        
        // Animate wobble phase for organic movement (legacy, kept for compatibility)
        colony->wobble_phase += 0.03f;
        if (colony->wobble_phase > 6.28318f) colony->wobble_phase -= 6.28318f;
        
        // Gradually evolve shape over time (slow, continuous morphing)
        colony->shape_evolution += 0.002f;
        if (colony->shape_evolution > 100.0f) colony->shape_evolution -= 100.0f;
    }
    
    world->tick++;
}
