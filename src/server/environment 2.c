#include "environment.h"
#include "../shared/simulation_common.h"
#include "../shared/utils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(FEROX_SIMD_AVX2)
#include <immintrin.h>
#endif

#if defined(FEROX_SIMD_NEON)
#include <arm_neon.h>
#endif

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
static void simd_sub_clamp01_inplace_avx2(float* values, int count, float sub) {
    const __m256 vsub = _mm256_set1_ps(sub);
    const __m256 vzero = _mm256_set1_ps(0.0f);
    const __m256 vone = _mm256_set1_ps(1.0f);
    int i = 0;
    for (; i + 8 <= count; i += 8) {
        __m256 v = _mm256_loadu_ps(values + i);
        v = _mm256_sub_ps(v, vsub);
        v = _mm256_max_ps(v, vzero);
        v = _mm256_min_ps(v, vone);
        _mm256_storeu_ps(values + i, v);
    }
    for (; i < count; i++) {
        values[i] = utils_clamp_f(values[i] - sub, 0.0f, 1.0f);
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

static void simd_sub_clamp01_inplace(float* values, int count, float sub) {
#if defined(FEROX_SIMD_AVX2) && (defined(__x86_64__) || defined(__i386__))
    if (simd_avx2_runtime_available()) {
        simd_sub_clamp01_inplace_avx2(values, count, sub);
        return;
    }
#elif defined(FEROX_SIMD_NEON)
    const float32x4_t vsub = vdupq_n_f32(sub);
    const float32x4_t vzero = vdupq_n_f32(0.0f);
    const float32x4_t vone = vdupq_n_f32(1.0f);
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t v = vld1q_f32(values + i);
        v = vsubq_f32(v, vsub);
        v = vmaxq_f32(v, vzero);
        v = vminq_f32(v, vone);
        vst1q_f32(values + i, v);
    }
    for (; i < count; i++) {
        values[i] = utils_clamp_f(values[i] - sub, 0.0f, 1.0f);
    }
    return;
#endif

    for (int i = 0; i < count; i++) {
        values[i] = utils_clamp_f(values[i] - sub, 0.0f, 1.0f);
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

static const int DX[] = {0, 1, 0, -1};
static const int DY[] = {-1, 0, 1, 0};

void simulation_update_nutrients(World* world) {
    if (!world || !world->nutrients) return;
    
    int total_cells = world->width * world->height;
    
    for (int i = 0; i < total_cells; i++) {
        if (world->cells[i].colony_id != 0) {
            Colony* colony = world_get_colony(world, world->cells[i].colony_id);
            float consumption = NUTRIENT_DEPLETION_RATE;
            if (colony) {
                consumption *= colony->genome.metabolism;
                consumption *= (1.0f - colony->genome.efficiency * 0.5f);
            }
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] - consumption, 0.0f, 1.0f);
        } else {
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] + NUTRIENT_REGEN_RATE, 0.0f, 1.0f);
        }
    }
}

void simulation_decay_toxins(World* world) {
    if (!world || !world->toxins) return;
    
    int total_cells = world->width * world->height;
    simd_sub_clamp01_inplace(world->toxins, total_cells, TOXIN_DECAY_RATE);
}

void simulation_produce_toxins(World* world) {
    if (!world || !world->toxins) return;
    
    int total_cells = world->width * world->height;
    simd_mul_inplace(world->toxins, total_cells, 0.95f);
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            float production = colony->genome.toxin_production * 
                               (1.0f + colony->genome.defense_priority * 0.5f);
            
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

void simulation_apply_toxin_damage(World* world) {
    if (!world || !world->toxins) return;
    
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
            
            float vulnerability = cell->is_border ? 
                (1.0f - colony->genome.defense_priority * 0.3f) : 1.0f;
            float damage = toxin_level * (1.0f - colony->genome.toxin_resistance) * vulnerability;
            
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

void simulation_consume_resources(World* world) {
    if (!world || !world->nutrients) return;
    
    int total = world->width * world->height;
    
    for (int i = 0; i < total; i++) {
        uint32_t cid = world->cells[i].colony_id;
        float n = world->nutrients[i];
        
        if (cid != 0) {
            Colony* colony = world_get_colony(world, cid);
            if (colony && colony->active) {
                float consumption = colony->genome.resource_consumption * 
                                    (0.5f + colony->genome.aggression * 0.5f);
                n -= consumption * 0.05f;
            }
            n += 0.002f;
        } else {
            n += 0.02f;
        }
        
        if (n < 0.0f) n = 0.0f;
        else if (n > 1.0f) n = 1.0f;
        world->nutrients[i] = n;
    }
    
    simd_sub_clamp01_inplace(world->toxins, total, 0.01f);
}

void simulation_update_scents(World* world) {
    if (!world || !world->signals || !world->signal_source) return;
    
    int total = world->width * world->height;
    
    float* new_signals = world->scratch_signals;
    uint32_t* new_sources = world->scratch_sources;
    if (!new_signals || !new_sources) return;
    
    memset(new_signals, 0, total * sizeof(float));
    memset(new_sources, 0, total * sizeof(uint32_t));
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int idx = y * world->width + x;
            Cell* cell = &world->cells[idx];
            if (cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            float emission = colony->genome.signal_emission * 0.3f;
            if (cell->is_border) emission *= 2.0f;
            emission *= (1.0f + (float)colony->cell_count / 500.0f);
            
            new_signals[idx] += emission;
            new_sources[idx] = cell->colony_id;
        }
    }
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int idx = y * world->width + x;
            float current = world->signals[idx];
            if (current < 0.001f) continue;
            
            uint32_t source = world->signal_source[idx];
            
            float keep = current * 0.6f;
            float spread = current * 0.075f;
            
            new_signals[idx] += keep;
            if (new_signals[idx] > 0 && source > 0) {
                new_sources[idx] = source;
            }
            
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (nx >= 0 && nx < world->width && ny >= 0 && ny < world->height) {
                    int ni = ny * world->width + nx;
                    new_signals[ni] += spread;
                    if (spread > 0.01f && source > 0) {
                        if (new_sources[ni] == 0 || new_signals[ni] < spread) {
                            new_sources[ni] = source;
                        }
                    }
                }
            }
        }
    }
    
    simd_clamp01_copy(world->signals, new_signals, total);
    memcpy(world->signal_source, new_sources, total * sizeof(uint32_t));
}
