/**
 * test_colors_exhaustive.c - Exhaustive color tests
 * Tests HSV to RGB conversion, color generation, and color distance
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/shared/colors.h"
#include "../src/shared/utils.h"

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
#define ASSERT_GT(a, b) ASSERT((a) > (b), #a " > " #b)
#define ASSERT_LT(a, b) ASSERT((a) < (b), #a " < " #b)
#define ASSERT_FLOAT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps), #a " ~= " #b)

// ============================================================================
// HSV to RGB Edge Cases
// ============================================================================

TEST(hsv_h_equals_zero) {
    // H=0, S=1, V=1 should be pure red
    Color c = hsv_to_rgb(0, 1.0f, 1.0f);
    ASSERT_EQ(c.r, 255);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 0);
}

TEST(hsv_h_equals_360) {
    // H=360 should wrap to same as H=0 (red)
    Color c = hsv_to_rgb(360, 1.0f, 1.0f);
    ASSERT_EQ(c.r, 255);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 0);
}

TEST(hsv_h_greater_than_360) {
    // H > 360 should wrap
    Color c1 = hsv_to_rgb(120, 1.0f, 1.0f);
    Color c2 = hsv_to_rgb(480, 1.0f, 1.0f);  // 480 - 360 = 120
    
    ASSERT_EQ(c1.r, c2.r);
    ASSERT_EQ(c1.g, c2.g);
    ASSERT_EQ(c1.b, c2.b);
}

TEST(hsv_h_negative) {
    // Negative H should wrap
    Color c1 = hsv_to_rgb(300, 1.0f, 1.0f);
    Color c2 = hsv_to_rgb(-60, 1.0f, 1.0f);  // -60 + 360 = 300
    
    ASSERT_EQ(c1.r, c2.r);
    ASSERT_EQ(c1.g, c2.g);
    ASSERT_EQ(c1.b, c2.b);
}

TEST(hsv_s_equals_zero) {
    // S=0 should be grayscale (no saturation)
    Color c = hsv_to_rgb(0, 0.0f, 1.0f);
    ASSERT_EQ(c.r, 255);
    ASSERT_EQ(c.g, 255);
    ASSERT_EQ(c.b, 255);
    
    c = hsv_to_rgb(0, 0.0f, 0.5f);
    // Gray should have equal components
    ASSERT_EQ(c.r, c.g);
    ASSERT_EQ(c.g, c.b);
}

TEST(hsv_s_equals_one) {
    // S=1 should be fully saturated
    Color c = hsv_to_rgb(0, 1.0f, 1.0f);  // Pure red
    ASSERT_EQ(c.r, 255);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 0);
    
    c = hsv_to_rgb(120, 1.0f, 1.0f);  // Pure green
    ASSERT_EQ(c.r, 0);
    ASSERT_EQ(c.g, 255);
    ASSERT_EQ(c.b, 0);
    
    c = hsv_to_rgb(240, 1.0f, 1.0f);  // Pure blue
    ASSERT_EQ(c.r, 0);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 255);
}

TEST(hsv_v_equals_zero) {
    // V=0 should always be black
    Color c = hsv_to_rgb(0, 1.0f, 0.0f);
    ASSERT_EQ(c.r, 0);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 0);
    
    c = hsv_to_rgb(180, 0.5f, 0.0f);
    ASSERT_EQ(c.r, 0);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 0);
}

TEST(hsv_v_equals_one) {
    // V=1 with full saturation should give primary colors
    Color c = hsv_to_rgb(60, 1.0f, 1.0f);  // Yellow
    ASSERT_EQ(c.r, 255);
    ASSERT_EQ(c.g, 255);
    ASSERT_EQ(c.b, 0);
    
    c = hsv_to_rgb(180, 1.0f, 1.0f);  // Cyan
    ASSERT_EQ(c.r, 0);
    ASSERT_EQ(c.g, 255);
    ASSERT_EQ(c.b, 255);
    
    c = hsv_to_rgb(300, 1.0f, 1.0f);  // Magenta
    ASSERT_EQ(c.r, 255);
    ASSERT_EQ(c.g, 0);
    ASSERT_EQ(c.b, 255);
}

TEST(hsv_all_hue_sectors) {
    // Test each 60-degree sector
    struct { float h; uint8_t exp_r, exp_g, exp_b; } tests[] = {
        {0,   255, 0,   0},    // Red
        {60,  255, 255, 0},    // Yellow
        {120, 0,   255, 0},    // Green
        {180, 0,   255, 255},  // Cyan
        {240, 0,   0,   255},  // Blue
        {300, 255, 0,   255},  // Magenta
    };
    
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        Color c = hsv_to_rgb(tests[i].h, 1.0f, 1.0f);
        ASSERT(c.r == tests[i].exp_r && c.g == tests[i].exp_g && c.b == tests[i].exp_b,
               "Wrong color for hue sector");
    }
}

// ============================================================================
// Body and Border Color Tests
// ============================================================================

TEST(body_border_sufficient_contrast) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Color body = generate_body_color();
        Color border = generate_border_color(body);
        
        float dist = color_distance(body, border);
        ASSERT_GT(dist, 30.0f);  // Should have noticeable contrast
    }
}

TEST(body_color_is_vibrant) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Color c = generate_body_color();
        
        // At least one component should be reasonably high
        int max_comp = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
        ASSERT_GE(max_comp, 100);
    }
}

TEST(border_darker_for_light_body) {
    // Light body color
    Color body = {200, 200, 200};
    Color border = generate_border_color(body);
    
    // Border should be darker
    int body_brightness = (body.r + body.g + body.b) / 3;
    int border_brightness = (border.r + border.g + border.b) / 3;
    
    ASSERT_LT(border_brightness, body_brightness);
}

TEST(border_lighter_for_dark_body) {
    // Dark body color
    Color body = {50, 50, 50};
    Color border = generate_border_color(body);
    
    // Border should be lighter
    int body_brightness = (body.r + body.g + body.b) / 3;
    int border_brightness = (border.r + border.g + border.b) / 3;
    
    ASSERT_GT(border_brightness, body_brightness);
}

// ============================================================================
// Color Distance Tests
// ============================================================================

TEST(color_distance_symmetric) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Color c1 = generate_body_color();
        Color c2 = generate_body_color();
        
        float dist12 = color_distance(c1, c2);
        float dist21 = color_distance(c2, c1);
        
        ASSERT_FLOAT_NEAR(dist12, dist21, 0.0001f);
    }
}

TEST(color_distance_identity) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Color c = generate_body_color();
        float dist = color_distance(c, c);
        
        ASSERT_FLOAT_NEAR(dist, 0.0f, 0.0001f);
    }
}

TEST(color_distance_black_white) {
    Color black = {0, 0, 0};
    Color white = {255, 255, 255};
    
    float dist = color_distance(black, white);
    
    // Max distance is sqrt(255^2 * 3) ≈ 441.67
    ASSERT(dist > 440.0f && dist < 442.0f, "Distance should be near max");
}

TEST(color_distance_extremes) {
    Color red = {255, 0, 0};
    Color green = {0, 255, 0};
    Color blue = {0, 0, 255};
    
    float rg = color_distance(red, green);
    float rb = color_distance(red, blue);
    float gb = color_distance(green, blue);
    
    // All should be equal distance (sqrt(255^2 * 2) ≈ 360.6)
    ASSERT_FLOAT_NEAR(rg, rb, 0.1f);
    ASSERT_FLOAT_NEAR(rb, gb, 0.1f);
    ASSERT(rg > 360.0f && rg < 361.0f, "Distance should be ~360.6");
}

TEST(color_distance_triangle_inequality) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        Color a = generate_body_color();
        Color b = generate_body_color();
        Color c = generate_body_color();
        
        float ab = color_distance(a, b);
        float bc = color_distance(b, c);
        float ac = color_distance(a, c);
        
        // Triangle inequality: ac <= ab + bc
        ASSERT(ac <= ab + bc + 0.001f, "Triangle inequality violated");
    }
}

// ============================================================================
// Random Color Generation Tests
// ============================================================================

TEST(random_colors_valid_range_1000) {
    rng_seed(42);
    
    for (int i = 0; i < 1000; i++) {
        Color c = generate_body_color();
        
        // RGB values are uint8_t so always 0-255 by type
        // Just verify generation works
        ASSERT_LE(c.r, 255);
        ASSERT_LE(c.g, 255);
        ASSERT_LE(c.b, 255);
    }
}

TEST(random_colors_have_variety) {
    rng_seed(42);
    
    Color colors[100];
    for (int i = 0; i < 100; i++) {
        colors[i] = generate_body_color();
    }
    
    // Check that not all colors are the same
    int different = 0;
    for (int i = 1; i < 100; i++) {
        if (colors[i].r != colors[0].r ||
            colors[i].g != colors[0].g ||
            colors[i].b != colors[0].b) {
            different++;
        }
    }
    
    ASSERT_GT(different, 80);  // At least 80% should be different
}

// ============================================================================
// Complementary Color Tests
// ============================================================================

TEST(complementary_colors_contrasting) {
    // Test that borders provide good contrast
    rng_seed(42);
    
    int good_contrast_count = 0;
    for (int i = 0; i < 100; i++) {
        Color body = generate_body_color();
        Color border = generate_border_color(body);
        
        float dist = color_distance(body, border);
        if (dist > 50.0f) {
            good_contrast_count++;
        }
    }
    
    // Most should have good contrast
    ASSERT_GE(good_contrast_count, 80);
}

// ============================================================================
// Clamp Tests
// ============================================================================

TEST(clamp_u8_boundaries) {
    ASSERT_EQ(clamp_u8(-100), 0);
    ASSERT_EQ(clamp_u8(-1), 0);
    ASSERT_EQ(clamp_u8(0), 0);
    ASSERT_EQ(clamp_u8(128), 128);
    ASSERT_EQ(clamp_u8(255), 255);
    ASSERT_EQ(clamp_u8(256), 255);
    ASSERT_EQ(clamp_u8(1000), 255);
}

// ============================================================================
// Color Blend Tests
// ============================================================================

TEST(color_blend_equal_weights) {
    Color red = {255, 0, 0};
    Color blue = {0, 0, 255};
    
    Color blended = color_blend(red, blue, 0.5f);
    
    // Should be approximately purple
    ASSERT(blended.r > 100 && blended.r < 140, "Red component ~127");
    ASSERT(blended.b > 100 && blended.b < 140, "Blue component ~127");
}

TEST(color_blend_full_weight_a) {
    Color red = {255, 0, 0};
    Color blue = {0, 0, 255};
    
    Color blended = color_blend(red, blue, 1.0f);
    
    ASSERT_EQ(blended.r, 255);
    ASSERT_EQ(blended.b, 0);
}

TEST(color_blend_full_weight_b) {
    Color red = {255, 0, 0};
    Color blue = {0, 0, 255};
    
    Color blended = color_blend(red, blue, 0.0f);
    
    ASSERT_EQ(blended.r, 0);
    ASSERT_EQ(blended.b, 255);
}

// ============================================================================
// HSV Edge Values
// ============================================================================

TEST(hsv_out_of_range_s_v_clamped) {
    // S and V > 1 should be clamped
    Color c1 = hsv_to_rgb(0, 2.0f, 1.0f);  // S clamped to 1
    Color c2 = hsv_to_rgb(0, 1.0f, 1.0f);
    ASSERT_EQ(c1.r, c2.r);
    
    // S and V < 0 should be clamped
    c1 = hsv_to_rgb(0, -1.0f, 1.0f);  // S clamped to 0
    c2 = hsv_to_rgb(0, 0.0f, 1.0f);
    ASSERT_EQ(c1.r, c2.r);
}

TEST(hsv_gradual_hue_change) {
    // Small hue changes should produce gradual color changes
    Color prev = hsv_to_rgb(0, 1.0f, 1.0f);
    
    for (int h = 1; h < 360; h++) {
        Color curr = hsv_to_rgb((float)h, 1.0f, 1.0f);
        float dist = color_distance(prev, curr);
        
        // Adjacent hues should have small distance
        ASSERT_LT(dist, 20.0f);
        prev = curr;
    }
}

// ============================================================================
// Run Tests
// ============================================================================

int run_colors_exhaustive_tests(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    printf("\n=== Colors Exhaustive Tests ===\n\n");
    
    printf("HSV to RGB Edge Cases:\n");
    RUN_TEST(hsv_h_equals_zero);
    RUN_TEST(hsv_h_equals_360);
    RUN_TEST(hsv_h_greater_than_360);
    RUN_TEST(hsv_h_negative);
    RUN_TEST(hsv_s_equals_zero);
    RUN_TEST(hsv_s_equals_one);
    RUN_TEST(hsv_v_equals_zero);
    RUN_TEST(hsv_v_equals_one);
    RUN_TEST(hsv_all_hue_sectors);
    
    printf("\nBody and Border Color Tests:\n");
    RUN_TEST(body_border_sufficient_contrast);
    RUN_TEST(body_color_is_vibrant);
    RUN_TEST(border_darker_for_light_body);
    RUN_TEST(border_lighter_for_dark_body);
    
    printf("\nColor Distance Tests:\n");
    RUN_TEST(color_distance_symmetric);
    RUN_TEST(color_distance_identity);
    RUN_TEST(color_distance_black_white);
    RUN_TEST(color_distance_extremes);
    RUN_TEST(color_distance_triangle_inequality);
    
    printf("\nRandom Color Generation Tests:\n");
    RUN_TEST(random_colors_valid_range_1000);
    RUN_TEST(random_colors_have_variety);
    
    printf("\nComplementary Color Tests:\n");
    RUN_TEST(complementary_colors_contrasting);
    
    printf("\nClamp Tests:\n");
    RUN_TEST(clamp_u8_boundaries);
    
    printf("\nColor Blend Tests:\n");
    RUN_TEST(color_blend_equal_weights);
    RUN_TEST(color_blend_full_weight_a);
    RUN_TEST(color_blend_full_weight_b);
    
    printf("\nHSV Edge Values:\n");
    RUN_TEST(hsv_out_of_range_s_v_clamped);
    RUN_TEST(hsv_gradual_hue_change);
    
    printf("\n--- Colors Exhaustive Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_colors_exhaustive_tests() > 0 ? 1 : 0;
}
#endif
