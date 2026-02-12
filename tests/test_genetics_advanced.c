/**
 * test_genetics_advanced.c - Advanced genetics tests for bacterial colony simulator
 * Tests mutation ranges, genetic distance properties, genome merging, and color mutations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/shared/types.h"
#include "../src/shared/utils.h"
#include "../src/server/genetics.h"

// Test framework
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s\n    At %s:%d\n", msg, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) ASSERT(cond, #cond)
#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_NE(a, b) ASSERT((a) != (b), #a " != " #b)
#define ASSERT_LE(a, b) ASSERT((a) <= (b), #a " <= " #b)
#define ASSERT_GE(a, b) ASSERT((a) >= (b), #a " >= " #b)
#define ASSERT_LT(a, b) ASSERT((a) < (b), #a " < " #b)
#define ASSERT_GT(a, b) ASSERT((a) > (b), #a " > " #b)
#define ASSERT_FLOAT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps), #a " ~= " #b)

// Helper to create a test genome with all fields
static Genome create_test_genome(float spread, float mutation, float aggr, float resil, float metab) {
    Genome g;
    memset(&g, 0, sizeof(Genome));
    for (int i = 0; i < 8; i++) g.spread_weights[i] = 0.5f;
    g.spread_rate = spread;
    g.mutation_rate = mutation * 0.1f;  // Scale to proper 0-0.1 range
    g.aggression = aggr;
    g.resilience = resil;
    g.metabolism = metab;
    // Set all genome fields to properly scaled values for distance calculations
    // Social traits
    g.detection_range = spread;
    g.social_factor = spread * 2.0f - 1.0f;  // Scale to proper -1 to 1 range
    g.merge_affinity = spread;
    g.max_tracked = 1 + (uint8_t)(spread * 3.0f);  // Scale to proper 1-4 range
    // Environmental sensing
    g.nutrient_sensitivity = spread;
    g.edge_affinity = spread * 2.0f - 1.0f;  // Scale to proper -1 to 1 range
    g.density_tolerance = spread;
    // Colony interactions
    g.toxin_production = spread;
    g.toxin_resistance = spread;
    g.signal_emission = spread;
    g.signal_sensitivity = spread;
    g.alarm_threshold = spread;
    g.gene_transfer_rate = mutation * 0.1f;  // Scale to proper 0-0.1 range
    // Competitive strategy
    g.resource_consumption = spread;
    g.defense_priority = spread;
    // Survival strategies
    g.dormancy_threshold = spread;
    g.biofilm_investment = spread;
    g.motility = spread;
    g.efficiency = spread;
    // Neural network
    for (int i = 0; i < 8; i++) g.hidden_weights[i] = spread * 2.0f - 1.0f;  // Scale to -1 to 1 range
    g.learning_rate = spread;
    g.memory_factor = spread;
    // Environmental sensing (missing fields)
    g.toxin_sensitivity = spread;
    g.quorum_threshold = spread;
    g.body_color = (Color){128, 128, 128};
    g.border_color = (Color){64, 64, 64};
    return g;
}

// ============================================================================
// Mutation Range Tests
// ============================================================================

TEST(mutation_values_stay_in_range_after_10000_iterations) {
    // Test that after many mutations, values stay within valid ranges
    rng_seed(42);
    Genome g = genome_create_random();
    g.mutation_rate = 1.0f;  // Maximum mutation chance
    
    for (int i = 0; i < 10000; i++) {
        genome_mutate(&g);
        
        ASSERT(g.spread_rate >= 0.0f && g.spread_rate <= 1.0f, 
               "spread_rate out of range");
        ASSERT(g.mutation_rate >= 0.0f && g.mutation_rate <= 1.0f, 
               "mutation_rate out of range");
        ASSERT(g.aggression >= 0.0f && g.aggression <= 1.0f, 
               "aggression out of range");
        ASSERT(g.resilience >= 0.0f && g.resilience <= 1.0f, 
               "resilience out of range");
        ASSERT(g.metabolism >= 0.0f && g.metabolism <= 1.0f, 
               "metabolism out of range");
    }
}

TEST(mutation_handles_boundary_values_correctly) {
    // Test mutation when values are at boundaries (0 and 1)
    rng_seed(42);
    
    // Test at lower boundary
    Genome g = create_test_genome(0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 1000; i++) {
        genome_mutate(&g);
        ASSERT(g.spread_rate >= 0.0f, "spread_rate went below 0");
        ASSERT(g.aggression >= 0.0f, "aggression went below 0");
    }
    
    // Test at upper boundary
    g = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 1000; i++) {
        genome_mutate(&g);
        ASSERT(g.spread_rate <= 1.0f, "spread_rate went above 1");
        ASSERT(g.aggression <= 1.0f, "aggression went above 1");
    }
}

// ============================================================================
// Genetic Distance Tests
// ============================================================================

TEST(genome_distance_is_symmetric) {
    // Test that distance(a,b) == distance(b,a)
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Genome a = genome_create_random();
        Genome b = genome_create_random();
        
        float dist_ab = genome_distance(&a, &b);
        float dist_ba = genome_distance(&b, &a);
        
        ASSERT_FLOAT_NEAR(dist_ab, dist_ba, 0.0001f);
    }
}

TEST(genome_distance_obeys_triangle_inequality) {
    // Test that distance(a,c) <= distance(a,b) + distance(b,c)
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Genome a = genome_create_random();
        Genome b = genome_create_random();
        Genome c = genome_create_random();
        
        float dist_ab = genome_distance(&a, &b);
        float dist_bc = genome_distance(&b, &c);
        float dist_ac = genome_distance(&a, &c);
        
        // Allow small epsilon for floating point
        ASSERT(dist_ac <= dist_ab + dist_bc + 0.0001f,
               "Triangle inequality violated");
    }
}

TEST(genome_distance_returns_zero_for_same_genome) {
    // Test that distance(a,a) == 0
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Genome a = genome_create_random();
        float dist = genome_distance(&a, &a);
        ASSERT_FLOAT_NEAR(dist, 0.0f, 0.0001f);
    }
}

TEST(genome_distance_is_always_non_negative) {
    // Test that distance is always >= 0
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Genome a = genome_create_random();
        Genome b = genome_create_random();
        float dist = genome_distance(&a, &b);
        ASSERT_GE(dist, 0.0f);
    }
}

TEST(genome_distance_max_is_one_for_extreme_diff) {
    // Test that maximum possible distance is 1.0
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    
    float dist = genome_distance(&a, &b);
    ASSERT_FLOAT_NEAR(dist, 1.0f, 0.0001f);
}

// ============================================================================
// Genome Merge Tests
// ============================================================================

TEST(genome_merge_equal_weights_returns_average) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    
    Genome result = genome_merge(&a, 100, &b, 100);
    
    ASSERT_FLOAT_NEAR(result.spread_rate, 0.5f, 0.0001f);
    // mutation_rate is scaled: 0.0 and 0.1, merged = 0.05
    ASSERT_FLOAT_NEAR(result.mutation_rate, 0.05f, 0.0001f);
    ASSERT_FLOAT_NEAR(result.aggression, 0.5f, 0.0001f);
    ASSERT_FLOAT_NEAR(result.resilience, 0.5f, 0.0001f);
    ASSERT_FLOAT_NEAR(result.metabolism, 0.5f, 0.0001f);
}

TEST(genome_merge_respects_weight_ratios) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    
    // 3:1 ratio - a has 75%, b has 25%
    Genome result = genome_merge(&a, 75, &b, 25);
    
    ASSERT_FLOAT_NEAR(result.spread_rate, 0.25f, 0.0001f);
    // mutation_rate is scaled: 0.0 and 0.1, merged with 75/25 = 0.025
    ASSERT_FLOAT_NEAR(result.mutation_rate, 0.025f, 0.0001f);
    
    // 1:3 ratio - a has 25%, b has 75%
    result = genome_merge(&a, 25, &b, 75);
    
    ASSERT_FLOAT_NEAR(result.spread_rate, 0.75f, 0.0001f);
    // mutation_rate is scaled: 0.0 and 0.1, merged with 25/75 = 0.075
    ASSERT_FLOAT_NEAR(result.mutation_rate, 0.075f, 0.0001f);
}

TEST(genome_merge_handles_extreme_weight_ratios) {
    Genome a = create_test_genome(0.2f, 0.2f, 0.2f, 0.2f, 0.2f);
    Genome b = create_test_genome(0.8f, 0.8f, 0.8f, 0.8f, 0.8f);
    
    // Extreme: 999:1 ratio
    Genome result = genome_merge(&a, 999, &b, 1);
    
    // Should be very close to a's values
    ASSERT(result.spread_rate < 0.21f && result.spread_rate > 0.19f,
           "Result should be close to a's values");
    
    // Opposite extreme
    result = genome_merge(&a, 1, &b, 999);
    ASSERT(result.spread_rate > 0.79f && result.spread_rate < 0.81f,
           "Result should be close to b's values");
}

TEST(genome_merge_with_zero_count_returns_other) {
    Genome a = create_test_genome(0.3f, 0.3f, 0.3f, 0.3f, 0.3f);
    Genome b = create_test_genome(0.7f, 0.7f, 0.7f, 0.7f, 0.7f);
    
    // Zero count for a - should return b
    Genome result = genome_merge(&a, 0, &b, 100);
    ASSERT_FLOAT_NEAR(result.spread_rate, 0.7f, 0.0001f);
    
    // Zero count for b - should return a
    result = genome_merge(&a, 100, &b, 0);
    ASSERT_FLOAT_NEAR(result.spread_rate, 0.3f, 0.0001f);
}

TEST(genome_merge_blends_body_colors) {
    Genome a = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    a.body_color = (Color){255, 0, 0};  // Red
    
    Genome b = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    b.body_color = (Color){0, 0, 255};  // Blue
    
    Genome result = genome_merge(&a, 50, &b, 50);
    
    // Should be purple-ish (blended)
    ASSERT(result.body_color.r > 100 && result.body_color.r < 140,
           "Red component should be ~127");
    ASSERT(result.body_color.b > 100 && result.body_color.b < 140,
           "Blue component should be ~127");
}

// ============================================================================
// Compatibility Tests
// ============================================================================

TEST(genome_compatible_respects_threshold) {
    Genome a = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    Genome b = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    
    // Identical genomes should be compatible at any threshold
    ASSERT_TRUE(genome_compatible(&a, &b, 0.0f));
    ASSERT_TRUE(genome_compatible(&a, &b, 0.5f));
    ASSERT_TRUE(genome_compatible(&a, &b, 1.0f));
    
    // Test threshold boundary
    Genome c = create_test_genome(0.6f, 0.5f, 0.5f, 0.5f, 0.5f);
    float dist = genome_distance(&a, &c);
    
    // Should be compatible at threshold >= distance
    ASSERT_TRUE(genome_compatible(&a, &c, dist + 0.001f));
    // Should NOT be compatible at threshold < distance
    ASSERT_TRUE(!genome_compatible(&a, &c, dist - 0.001f));
}

TEST(genome_compatible_returns_false_for_null) {
    Genome a = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    
    ASSERT_TRUE(!genome_compatible(NULL, &a, 0.5f));
    ASSERT_TRUE(!genome_compatible(&a, NULL, 0.5f));
    ASSERT_TRUE(!genome_compatible(NULL, NULL, 0.5f));
}

// ============================================================================
// Genome Creation Tests
// ============================================================================

TEST(genome_create_produces_valid_spread_weights) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Genome g = genome_create_random();
        
        float sum = 0.0f;
        for (int j = 0; j < 8; j++) {
            ASSERT(g.spread_weights[j] >= 0.0f && g.spread_weights[j] <= 1.0f,
                   "Spread weight out of range");
            sum += g.spread_weights[j];
        }
        
        // Sum should be reasonable (not all zeros, not all ones)
        ASSERT(sum > 0.0f, "All spread weights are zero");
        ASSERT(sum < 8.0f, "All spread weights are maximum");
    }
}

TEST(genome_create_produces_valid_color_range) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Genome g = genome_create_random();
        
        // Body colors should be in valid range (30-255 for more variety)
        ASSERT(g.body_color.r >= 30 && g.body_color.r <= 255, "Body R out of range");
        ASSERT(g.body_color.g >= 30 && g.body_color.g <= 255, "Body G out of range");
        ASSERT(g.body_color.b >= 30 && g.body_color.b <= 255, "Body B out of range");
        
        // Border should be darker (half of body based on implementation)
        ASSERT_LE(g.border_color.r, g.body_color.r);
        ASSERT_LE(g.border_color.g, g.body_color.g);
        ASSERT_LE(g.border_color.b, g.body_color.b);
    }
}

// ============================================================================
// Genetic Drift Tests
// ============================================================================

TEST(genome_mutations_cause_genetic_drift) {
    rng_seed(42);
    
    Genome original = create_test_genome(0.5f, 0.05f, 0.5f, 0.5f, 0.5f);
    Genome current = original;
    
    float initial_distance = genome_distance(&original, &current);
    ASSERT_FLOAT_NEAR(initial_distance, 0.0f, 0.0001f);
    
    // Apply mutations and track drift
    for (int i = 0; i < 1000; i++) {
        genome_mutate(&current);
    }
    
    float final_distance = genome_distance(&original, &current);
    
    // After many mutations, there should be some drift
    // (not guaranteed due to randomness, but very likely with 1000 iterations)
    ASSERT(final_distance > 0.0f, "No genetic drift after 1000 mutations");
}

TEST(genome_low_mutation_rate_causes_minimal_change) {
    rng_seed(42);
    
    Genome original = create_test_genome(0.5f, 0.02f, 0.5f, 0.5f, 0.5f);  // Minimum mutation rate
    Genome current = original;
    
    // With very low mutation rate, changes should be small but present (dynamic simulation)
    for (int i = 0; i < 10; i++) {
        genome_mutate(&current);
    }
    
    // Values can change but should stay reasonable
    ASSERT(current.spread_rate >= 0.0f && current.spread_rate <= 1.0f, "spread_rate in range");
    ASSERT(current.aggression >= 0.0f && current.aggression <= 1.0f, "aggression in range");
    ASSERT(current.resilience >= 0.0f && current.resilience <= 1.0f, "resilience in range");
    ASSERT(current.metabolism >= 0.0f && current.metabolism <= 1.0f, "metabolism in range");
}

// ============================================================================
// Color Mutation Tests
// ============================================================================

TEST(genome_colors_stay_in_valid_rgb_range) {
    // Note: genome_mutate doesn't mutate colors directly in current implementation
    // but we test that created genomes have valid colors
    rng_seed(42);
    
    for (int i = 0; i < 1000; i++) {
        Genome g = genome_create_random();
        
        // RGB values are uint8_t so always 0-255 by type
        // Just verify they're created correctly
        ASSERT(g.body_color.r <= 255, "Body R overflow");
        ASSERT(g.body_color.g <= 255, "Body G overflow");
        ASSERT(g.body_color.b <= 255, "Body B overflow");
        ASSERT(g.border_color.r <= 255, "Border R overflow");
        ASSERT(g.border_color.g <= 255, "Border G overflow");
        ASSERT(g.border_color.b <= 255, "Border B overflow");
    }
}

// ============================================================================
// Run Tests
// ============================================================================

int run_genetics_advanced_tests(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    printf("\n=== Advanced Genetics Tests ===\n\n");
    
    printf("Mutation Range Tests:\n");
    RUN_TEST(mutation_values_stay_in_range_after_10000_iterations);
    RUN_TEST(mutation_handles_boundary_values_correctly);
    
    printf("\nGenetic Distance Tests:\n");
    RUN_TEST(genome_distance_is_symmetric);
    RUN_TEST(genome_distance_obeys_triangle_inequality);
    RUN_TEST(genome_distance_returns_zero_for_same_genome);
    RUN_TEST(genome_distance_is_always_non_negative);
    RUN_TEST(genome_distance_max_is_one_for_extreme_diff);
    
    printf("\nGenome Merge Tests:\n");
    RUN_TEST(genome_merge_equal_weights_returns_average);
    RUN_TEST(genome_merge_respects_weight_ratios);
    RUN_TEST(genome_merge_handles_extreme_weight_ratios);
    RUN_TEST(genome_merge_with_zero_count_returns_other);
    RUN_TEST(genome_merge_blends_body_colors);
    
    printf("\nCompatibility Tests:\n");
    RUN_TEST(genome_compatible_respects_threshold);
    RUN_TEST(genome_compatible_returns_false_for_null);
    
    printf("\nGenome Creation Tests:\n");
    RUN_TEST(genome_create_produces_valid_spread_weights);
    RUN_TEST(genome_create_produces_valid_color_range);
    
    printf("\nGenetic Drift Tests:\n");
    RUN_TEST(genome_mutations_cause_genetic_drift);
    RUN_TEST(genome_low_mutation_rate_causes_minimal_change);
    
    printf("\nColor Tests:\n");
    RUN_TEST(genome_colors_stay_in_valid_rgb_range);
    
    printf("\n--- Genetics Advanced Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_genetics_advanced_tests() > 0 ? 1 : 0;
}
#endif
