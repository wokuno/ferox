#ifndef FEROX_UTILS_H
#define FEROX_UTILS_H

#include <stdint.h>
#include <math.h>

// Initialize the random number generator with a seed
void rng_seed(uint64_t seed);

// Generate a random float in range [0, 1)
float rand_float(void);

// Generate a random integer in range [0, max)
int rand_int(int max);

// Generate a random integer in range [min, max]
int rand_range(int min, int max);

// Clamp float to range
static inline float utils_clamp_f(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

// Clamp int to range
static inline int utils_clamp_i(int val, int min, int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

// Absolute value for float
static inline float utils_abs_f(float val) {
    return val < 0 ? -val : val;
}

// Sigmoid activation function for neural network (returns 0-1)
static inline float sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

// Fast sigmoid approximation for neural network (returns 0-1)
static inline float sigmoid_fast(float x) {
    return x / (1.0f + fabsf(x)) * 0.5f + 0.5f;
}

// ============================================================================
// Procedural noise functions for colony shape generation
// ============================================================================

// Hash function for procedural generation (deterministic)
static inline uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x85ebca6b;
    x ^= x >> 13;
    x *= 0xc2b2ae35;
    x ^= x >> 16;
    return x;
}

// Convert hash to float in range [0, 1)
static inline float hash_to_float(uint32_t h) {
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

// 1D noise from seed and position (returns -1 to 1)
static inline float noise1d(uint32_t seed, float x) {
    int xi = (int)x;
    float xf = x - (float)xi;
    
    // Smooth interpolation
    float t = xf * xf * (3.0f - 2.0f * xf);
    
    float v0 = hash_to_float(hash_u32(seed ^ (uint32_t)xi)) * 2.0f - 1.0f;
    float v1 = hash_to_float(hash_u32(seed ^ (uint32_t)(xi + 1))) * 2.0f - 1.0f;
    
    return v0 + t * (v1 - v0);
}

// Multi-octave noise for organic shapes (returns -1 to 1)
static inline float fractal_noise1d(uint32_t seed, float x, int octaves) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float max_value = 0.0f;
    
    for (int i = 0; i < octaves; i++) {
        value += noise1d(seed + (uint32_t)i * 12345, x * frequency) * amplitude;
        max_value += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    
    return value / max_value;
}

// Get colony shape radius multiplier at a given angle
// Returns a value typically in range [0.5, 1.5] for organic blob shapes
// evolution: 0+ value that causes shape to morph over time
static inline float colony_shape_at_angle_evolved(uint32_t shape_seed, float angle, float phase, float evolution) {
    // Normalize angle to [0, 2*PI)
    const float TWO_PI = 6.28318530718f;
    while (angle < 0) angle += TWO_PI;
    while (angle >= TWO_PI) angle -= TWO_PI;
    
    // Use evolution to gradually shift the shape parameters
    // Evolution creates smooth morphing by blending derived values
    float evo_factor = sinf(evolution * 0.5f) * 0.5f + 0.5f;  // 0-1 oscillating
    float evo_shift = evolution * 0.1f;  // Continuous shift
    
    // Derive shape parameters from seed, but shift them with evolution
    uint32_t evo_seed = shape_seed + (uint32_t)(evolution * 1000.0f);
    uint32_t h1 = hash_u32(shape_seed);
    uint32_t h2 = hash_u32(shape_seed ^ 0xDEADBEEF);
    uint32_t h3 = hash_u32(shape_seed ^ 0xCAFEBABE);
    uint32_t h4 = hash_u32(shape_seed ^ 0x12345678);
    uint32_t h5 = hash_u32(shape_seed ^ 0xFEEDFACE);
    
    // Shape type: determines base character (0-7 different base shapes)
    uint32_t shape_type = h1 % 8;
    
    float result = 1.0f;
    
    switch (shape_type) {
        case 0: {
            // Round blob - nearly circular with subtle wobble
            float wobble = fractal_noise1d(shape_seed, angle / TWO_PI * 6.0f, 3);
            result = 1.0f + wobble * 0.15f;
            break;
        }
        case 1: {
            // Elongated ellipse
            float elong = 0.2f + hash_to_float(h2) * 0.4f;  // 0.2 to 0.6 elongation
            float elong_angle = hash_to_float(h3) * TWO_PI;
            float cos_diff = cosf(angle - elong_angle);
            result = 1.0f + elong * cos_diff * cos_diff - elong * 0.3f;
            break;
        }
        case 2: {
            // Lobed (3-6 lobes like a flower)
            uint32_t lobes = 3 + (h2 % 4);
            float lobe_depth = 0.15f + hash_to_float(h3) * 0.25f;
            float lobe_angle = hash_to_float(h4) * TWO_PI;
            result = 1.0f + sinf(angle * (float)lobes + lobe_angle) * lobe_depth;
            break;
        }
        case 3: {
            // Amoeba-like (irregular with multiple bulges)
            float noise = fractal_noise1d(shape_seed, angle / TWO_PI * 12.0f, 5);
            result = 1.0f + noise * 0.35f;
            break;
        }
        case 4: {
            // Star-like (pointed projections)
            uint32_t points = 4 + (h2 % 5);  // 4-8 points
            float point_depth = 0.2f + hash_to_float(h3) * 0.2f;
            float star_angle = hash_to_float(h4) * TWO_PI / (float)points;
            float val = cosf(angle * (float)points + star_angle);
            result = 1.0f + (val > 0 ? val * val : 0) * point_depth - point_depth * 0.3f;
            break;
        }
        case 5: {
            // Bean/kidney shape (asymmetric)
            float elong_angle = hash_to_float(h2) * TWO_PI;
            float indent_angle = elong_angle + 3.14159f * (0.3f + hash_to_float(h3) * 0.4f);
            float cos_elong = cosf(angle - elong_angle);
            float cos_indent = cosf(angle - indent_angle);
            result = 1.0f + cos_elong * cos_elong * 0.25f - (cos_indent > 0.7f ? 0.15f : 0.0f);
            break;
        }
        case 6: {
            // Wavy/ruffled edge
            float base_noise = fractal_noise1d(shape_seed, angle / TWO_PI * 4.0f, 2);
            float ruffle = sinf(angle * (8.0f + (float)(h2 % 8))) * 0.1f;
            result = 1.0f + base_noise * 0.2f + ruffle;
            break;
        }
        case 7: {
            // Combined: elongated with lobes
            float elong = 0.15f + hash_to_float(h2) * 0.2f;
            float elong_angle = hash_to_float(h3) * TWO_PI;
            uint32_t lobes = 2 + (h4 % 3);
            float lobe_depth = 0.1f + hash_to_float(h5) * 0.15f;
            float cos_diff = cosf(angle - elong_angle);
            result = 1.0f + elong * cos_diff * cos_diff - elong * 0.3f 
                   + sinf(angle * (float)lobes) * lobe_depth;
            break;
        }
    }
    
    // Add subtle animation (breathing effect)
    float anim = sinf(phase * 2.0f + angle * 1.5f) * 0.03f;
    result += anim;
    
    // Add evolution-based morphing - smooth slow changes over time
    float evo_morph = sinf(angle * 3.0f + evo_shift) * 0.1f * evo_factor;
    float evo_wobble = fractal_noise1d(evo_seed, angle / TWO_PI * 8.0f, 3) * 0.15f * evo_factor;
    result += evo_morph + evo_wobble;
    
    // Add very subtle high-frequency detail
    float detail = fractal_noise1d(shape_seed ^ 0xFF, angle / TWO_PI * 16.0f, 2) * 0.05f;
    result += detail;
    
    // Clamp to prevent extreme shapes
    if (result < 0.5f) result = 0.5f;
    if (result > 1.5f) result = 1.5f;
    
    return result;
}

// Legacy wrapper for backward compatibility
static inline float colony_shape_at_angle(uint32_t shape_seed, float angle, float phase) {
    return colony_shape_at_angle_evolved(shape_seed, angle, phase, 0.0f);
}

#endif // FEROX_UTILS_H
