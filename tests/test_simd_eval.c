/**
 * test_simd_eval.c - SIMD correctness/performance evaluation tests
 * Validates SIMD-accelerated paths match scalar behavior and reports timings.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "../src/shared/types.h"
#include "../src/shared/utils.h"
#include "../src/server/world.h"
#include "../src/server/genetics.h"
#include "../src/server/simulation.h"

// Exposed in simulation.c (not in simulation.h yet)
extern void simulation_decay_toxins(World* world);
extern void simulation_produce_toxins(World* world);

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
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL, #ptr " is not NULL")

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void scalar_decay_clamp(float* values, int count, float sub) {
    for (int i = 0; i < count; i++) {
        values[i] = utils_clamp_f(values[i] - sub, 0.0f, 1.0f);
    }
}

static void scalar_mul(float* values, int count, float factor) {
    for (int i = 0; i < count; i++) {
        values[i] *= factor;
    }
}

TEST(simd_decay_toxins_matches_scalar_reference) {
    World* world = world_create(257, 129);  // non-multiple tail coverage
    ASSERT_NOT_NULL(world);
    ASSERT_NOT_NULL(world->toxins);

    int total = world->width * world->height;
    float* expected = (float*)malloc((size_t)total * sizeof(float));
    ASSERT_NOT_NULL(expected);

    rng_seed(123);
    for (int i = 0; i < total; i++) {
        world->toxins[i] = rand_float();
        expected[i] = world->toxins[i];
    }

    simulation_decay_toxins(world);
    scalar_decay_clamp(expected, total, 0.01f);

    for (int i = 0; i < total; i++) {
        float diff = fabsf(world->toxins[i] - expected[i]);
        ASSERT(diff < 1e-6f, "SIMD decay mismatch vs scalar reference");
    }

    free(expected);
    world_destroy(world);
}

TEST(simd_produce_toxins_decay_matches_scalar_on_empty_world) {
    World* world = world_create(300, 200);
    ASSERT_NOT_NULL(world);
    ASSERT_NOT_NULL(world->toxins);

    int total = world->width * world->height;
    float* expected = (float*)malloc((size_t)total * sizeof(float));
    ASSERT_NOT_NULL(expected);

    rng_seed(321);
    for (int i = 0; i < total; i++) {
        world->toxins[i] = rand_float();
        expected[i] = world->toxins[i];
    }

    // No colonies/cells -> function should only apply 0.95 decay pass.
    simulation_produce_toxins(world);
    scalar_mul(expected, total, 0.95f);

    for (int i = 0; i < total; i++) {
        float diff = fabsf(world->toxins[i] - expected[i]);
        ASSERT(diff < 1e-6f, "SIMD multiply mismatch vs scalar reference");
    }

    free(expected);
    world_destroy(world);
}

TEST(simd_update_scents_clamps_signal_range) {
    World* world = world_create(128, 96);
    ASSERT_NOT_NULL(world);
    ASSERT_NOT_NULL(world->signals);
    ASSERT_NOT_NULL(world->signal_source);

    int total = world->width * world->height;
    rng_seed(777);

    // Fill with out-of-range values to ensure clamp path is exercised.
    for (int i = 0; i < total; i++) {
        world->signals[i] = -1.0f + rand_float() * 4.0f;  // [-1, 3]
        world->signal_source[i] = 0;
    }

    // Add one colony with emitting border cells.
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.genome = genome_create_random();
    colony.genome.signal_emission = 1.0f;
    colony.active = true;
    colony.cell_count = 3;
    uint32_t id = world_add_colony(world, colony);
    ASSERT_TRUE(id != 0);

    int cx = world->width / 2;
    int cy = world->height / 2;
    Cell* c0 = world_get_cell(world, cx, cy);
    Cell* c1 = world_get_cell(world, cx + 1, cy);
    Cell* c2 = world_get_cell(world, cx, cy + 1);
    ASSERT_NOT_NULL(c0); ASSERT_NOT_NULL(c1); ASSERT_NOT_NULL(c2);
    c0->colony_id = id; c0->is_border = true;
    c1->colony_id = id; c1->is_border = true;
    c2->colony_id = id; c2->is_border = true;

    simulation_update_scents(world);

    for (int i = 0; i < total; i++) {
        ASSERT(world->signals[i] >= 0.0f && world->signals[i] <= 1.0f,
               "signals must be clamped to [0,1]");
    }

    world_destroy(world);
}

TEST(simd_decay_toxins_performance_eval) {
    World* world = world_create(512, 512);
    ASSERT_NOT_NULL(world);
    ASSERT_NOT_NULL(world->toxins);

    const int total = world->width * world->height;
    const int iters = 120;

    float* scalar_buf = (float*)malloc((size_t)total * sizeof(float));
    ASSERT_NOT_NULL(scalar_buf);

    rng_seed(999);
    for (int i = 0; i < total; i++) {
        float v = rand_float();
        world->toxins[i] = v;
        scalar_buf[i] = v;
    }

    // Warmup
    simulation_decay_toxins(world);
    scalar_decay_clamp(scalar_buf, total, 0.01f);

    // Reset
    rng_seed(999);
    for (int i = 0; i < total; i++) {
        float v = rand_float();
        world->toxins[i] = v;
        scalar_buf[i] = v;
    }

    double t0 = now_ms();
    for (int i = 0; i < iters; i++) simulation_decay_toxins(world);
    double t1 = now_ms();

    double t2 = now_ms();
    for (int i = 0; i < iters; i++) scalar_decay_clamp(scalar_buf, total, 0.01f);
    double t3 = now_ms();

    double simd_ms = t1 - t0;
    double scalar_ms = t3 - t2;
    printf("\n    [perf] decay_toxins: optimized=%.2fms scalar=%.2fms ratio=%.2fx\n",
           simd_ms, scalar_ms, scalar_ms > 0.0 ? simd_ms / scalar_ms : 0.0);

    // Keep this non-flaky while still guarding major regressions.
    ASSERT(simd_ms <= scalar_ms * 8.0 + 1.0,
           "optimized decay path unexpectedly much slower than scalar");

    free(scalar_buf);
    world_destroy(world);
}

int run_simd_eval_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("\n=== SIMD Eval Tests ===\n\n");
    RUN_TEST(simd_decay_toxins_matches_scalar_reference);
    RUN_TEST(simd_produce_toxins_decay_matches_scalar_on_empty_world);
    RUN_TEST(simd_update_scents_clamps_signal_range);
    RUN_TEST(simd_decay_toxins_performance_eval);

    printf("\n--- SIMD Eval Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_simd_eval_tests() > 0 ? 1 : 0;
}
#endif

