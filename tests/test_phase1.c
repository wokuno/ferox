#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "../src/shared/types.h"
#include "../src/shared/utils.h"
#include "../src/shared/names.h"
#include "../src/shared/colors.h"

// Simple test framework
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Assertion failed: %s\n    At %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_LT(a, b) ASSERT((a) < (b))
#define ASSERT_LE(a, b) ASSERT((a) <= (b))
#define ASSERT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_GE(a, b) ASSERT((a) >= (b))

// ============ Utility Tests ============

TEST(rng_seed_deterministic) {
    rng_seed(12345);
    float a1 = rand_float();
    float a2 = rand_float();
    
    rng_seed(12345);
    float b1 = rand_float();
    float b2 = rand_float();
    
    ASSERT_EQ(a1, b1);
    ASSERT_EQ(a2, b2);
}

TEST(rand_float_range) {
    rng_seed(42);
    for (int i = 0; i < 1000; i++) {
        float f = rand_float();
        ASSERT_GE(f, 0.0f);
        ASSERT_LT(f, 1.0f);
    }
}

TEST(rand_int_range) {
    rng_seed(42);
    for (int i = 0; i < 1000; i++) {
        int n = rand_int(100);
        ASSERT_GE(n, 0);
        ASSERT_LT(n, 100);
    }
}

TEST(rand_int_zero_max) {
    int n = rand_int(0);
    ASSERT_EQ(n, 0);
}

TEST(rand_range_bounds) {
    rng_seed(42);
    for (int i = 0; i < 1000; i++) {
        int n = rand_range(10, 20);
        ASSERT_GE(n, 10);
        ASSERT_LE(n, 20);
    }
}

TEST(rand_range_equal_bounds) {
    int n = rand_range(5, 5);
    ASSERT_EQ(n, 5);
}

// ============ Name Generation Tests ============

TEST(name_generation_format) {
    rng_seed(42);
    char name[64];
    generate_scientific_name(name, sizeof(name));
    
    // Name should contain a space (genus + species)
    ASSERT(strchr(name, ' ') != NULL);
    
    // Name should be non-empty
    ASSERT(strlen(name) > 5);
}

TEST(name_generation_uniqueness) {
    rng_seed(42);
    char name1[64], name2[64], name3[64];
    
    generate_scientific_name(name1, sizeof(name1));
    generate_scientific_name(name2, sizeof(name2));
    generate_scientific_name(name3, sizeof(name3));
    
    // At least 2 of 3 should be different (with high probability)
    bool all_same = (strcmp(name1, name2) == 0) && 
                    (strcmp(name2, name3) == 0);
    ASSERT(!all_same);
}

TEST(name_generation_buffer) {
    char name[64];
    memset(name, 'X', sizeof(name));
    
    generate_scientific_name(name, sizeof(name));
    
    // Should be null-terminated
    ASSERT(name[63] == '\0' || strlen(name) < 64);
}

// ============ Color Tests ============

TEST(hsv_to_rgb_red) {
    Color c = hsv_to_rgb(0, 1.0f, 1.0f);
    ASSERT_EQ(c.r, 255);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 0);
}

TEST(hsv_to_rgb_green) {
    Color c = hsv_to_rgb(120, 1.0f, 1.0f);
    ASSERT_EQ(c.r, 0);
    ASSERT_EQ(c.g, 255);
    ASSERT_EQ(c.b, 0);
}

TEST(hsv_to_rgb_blue) {
    Color c = hsv_to_rgb(240, 1.0f, 1.0f);
    ASSERT_EQ(c.r, 0);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 255);
}

TEST(hsv_to_rgb_white) {
    Color c = hsv_to_rgb(0, 0.0f, 1.0f);
    ASSERT_EQ(c.r, 255);
    ASSERT_EQ(c.g, 255);
    ASSERT_EQ(c.b, 255);
}

TEST(hsv_to_rgb_black) {
    Color c = hsv_to_rgb(0, 0.0f, 0.0f);
    ASSERT_EQ(c.r, 0);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 0);
}

TEST(body_color_valid_range) {
    rng_seed(42);
    for (int i = 0; i < 100; i++) {
        Color c = generate_body_color();
        // RGB values are always valid (0-255 by type)
        // Check that colors are reasonably vibrant
        int max_component = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
        ASSERT_GT(max_component, 50);  // Should have some brightness
    }
}

TEST(border_color_contrasting) {
    rng_seed(42);
    for (int i = 0; i < 100; i++) {
        Color body = generate_body_color();
        Color border = generate_border_color(body);
        
        float dist = color_distance(body, border);
        ASSERT_GT(dist, 30.0f);  // Should have some contrast
    }
}

TEST(color_distance_same) {
    Color c = {100, 150, 200};
    float dist = color_distance(c, c);
    ASSERT_EQ(dist, 0.0f);
}

TEST(color_distance_max) {
    Color black = {0, 0, 0};
    Color white = {255, 255, 255};
    float dist = color_distance(black, white);
    // Max distance is sqrt(255^2 * 3) ≈ 441.67
    ASSERT_GT(dist, 440.0f);
    ASSERT_LT(dist, 442.0f);
}

TEST(clamp_u8_boundaries) {
    ASSERT_EQ(clamp_u8(-10), 0);
    ASSERT_EQ(clamp_u8(0), 0);
    ASSERT_EQ(clamp_u8(128), 128);
    ASSERT_EQ(clamp_u8(255), 255);
    ASSERT_EQ(clamp_u8(300), 255);
}

// ============ Genome Tests ============

TEST(genome_struct_size) {
    Genome g;
    // Verify structure has expected fields
    ASSERT_EQ(sizeof(g.spread_weights) / sizeof(float), 8);
    
    // Initialize and check
    for (int i = 0; i < 8; i++) {
        g.spread_weights[i] = 0.5f;
    }
    g.mutation_rate = 0.05f;
    g.aggression = 0.7f;
    g.resilience = 0.3f;
    g.body_color = (Color){255, 128, 64};
    g.border_color = (Color){64, 32, 16};
    
    ASSERT_EQ(g.spread_weights[DIR_N], 0.5f);
    ASSERT_EQ(g.spread_weights[DIR_SE], 0.5f);
    ASSERT_EQ(g.mutation_rate, 0.05f);
    ASSERT_EQ(g.body_color.r, 255);
}

TEST(colony_struct_init) {
    Colony colony = {0};
    
    colony.id = 1;
    strncpy(colony.name, "Bacillus testii", sizeof(colony.name) - 1);
    colony.cell_count = 100;
    colony.age = 500;
    colony.parent_id = 0;
    
    ASSERT_EQ(colony.id, 1);
    ASSERT_EQ(strcmp(colony.name, "Bacillus testii"), 0);
    ASSERT_EQ(colony.cell_count, 100);
    ASSERT_EQ(colony.age, 500);
    ASSERT_EQ(colony.parent_id, 0);
}

TEST(cell_struct_init) {
    Cell cell = {0};
    
    ASSERT_EQ(cell.colony_id, 0);
    ASSERT_EQ(cell.is_border, false);
    
    cell.colony_id = 42;
    cell.is_border = true;
    
    ASSERT_EQ(cell.colony_id, 42);
    ASSERT_EQ(cell.is_border, true);
}

TEST(direction_enum_values) {
    ASSERT_EQ(DIR_N, 0);
    ASSERT_EQ(DIR_NE, 1);
    ASSERT_EQ(DIR_E, 2);
    ASSERT_EQ(DIR_SE, 3);
    ASSERT_EQ(DIR_S, 4);
    ASSERT_EQ(DIR_SW, 5);
    ASSERT_EQ(DIR_W, 6);
    ASSERT_EQ(DIR_NW, 7);
    ASSERT_EQ(DIR_COUNT, 8);
}

// ============ Main ============

int main(void) {
    printf("\n=== Phase 1 Unit Tests ===\n\n");
    
    printf("Random Utility Tests:\n");
    RUN_TEST(rng_seed_deterministic);
    RUN_TEST(rand_float_range);
    RUN_TEST(rand_int_range);
    RUN_TEST(rand_int_zero_max);
    RUN_TEST(rand_range_bounds);
    RUN_TEST(rand_range_equal_bounds);
    
    printf("\nName Generation Tests:\n");
    RUN_TEST(name_generation_format);
    RUN_TEST(name_generation_uniqueness);
    RUN_TEST(name_generation_buffer);
    
    printf("\nColor Tests:\n");
    RUN_TEST(hsv_to_rgb_red);
    RUN_TEST(hsv_to_rgb_green);
    RUN_TEST(hsv_to_rgb_blue);
    RUN_TEST(hsv_to_rgb_white);
    RUN_TEST(hsv_to_rgb_black);
    RUN_TEST(body_color_valid_range);
    RUN_TEST(border_color_contrasting);
    RUN_TEST(color_distance_same);
    RUN_TEST(color_distance_max);
    RUN_TEST(clamp_u8_boundaries);
    
    printf("\nGenome & Colony Tests:\n");
    RUN_TEST(genome_struct_size);
    RUN_TEST(colony_struct_init);
    RUN_TEST(cell_struct_init);
    RUN_TEST(direction_enum_values);
    
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("\n");
    
    return tests_failed > 0 ? 1 : 0;
}
