#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/shared/utils.h"
#include "../src/server/world.h"
#include "../src/server/simulation.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int failed_before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == failed_before) { \
        printf("PASSED\n"); \
        tests_passed++; \
    } \
} while (0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s\n    At %s:%d\n", msg, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) <= (eps), #a " ~= " #b)

static Colony make_monod_test_colony(void) {
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.active = true;
    colony.genome.spread_rate = 0.18f;
    colony.genome.metabolism = 1.0f;
    colony.genome.efficiency = 0.0f;
    colony.genome.resource_consumption = 1.0f;
    colony.genome.quorum_threshold = 1.0f;
    colony.genome.density_tolerance = 1.0f;
    for (int i = 0; i < 8; i++) {
        colony.genome.spread_weights[i] = 1.0f;
    }
    return colony;
}

static float run_single_cell_uptake(float substrate, const MonodKineticsConfig* config) {
    World* world = world_create(1, 1);
    if (!world) return -1.0f;

    if (config) {
        world_set_monod_kinetics(world, config);
    }

    Colony colony = make_monod_test_colony();
    uint32_t id = world_add_colony(world, colony);
    world->cells[0].colony_id = id;
    world->cells[0].age = 1;
    world_get_colony(world, id)->cell_count = 1;
    world->nutrients[0] = substrate;

    simulation_update_nutrients(world);
    float consumed = substrate - world->nutrients[0];

    world_destroy(world);
    return consumed;
}

static int run_spread_steps(float substrate, const MonodKineticsConfig* config, int steps, uint64_t seed) {
    World* world = world_create(21, 21);
    if (!world) return -1;

    world_set_monod_kinetics(world, config);
    for (int i = 0; i < world->width * world->height; i++) {
        world->nutrients[i] = substrate;
        world->toxins[i] = 0.0f;
    }

    Colony colony = make_monod_test_colony();
    colony.genome.nutrient_sensitivity = 0.0f;
    colony.genome.toxin_sensitivity = 0.0f;
    colony.genome.edge_affinity = 0.0f;
    uint32_t id = world_add_colony(world, colony);

    int cx = world->width / 2;
    int cy = world->height / 2;
    world_get_cell(world, cx, cy)->colony_id = id;
    world_get_cell(world, cx, cy)->age = 1;
    world_get_colony(world, id)->cell_count = 1;

    rng_seed(seed);
    for (int i = 0; i < steps; i++) {
        simulation_spread(world);
    }

    int final_cells = (int)world_get_colony(world, id)->cell_count;
    world_destroy(world);
    return final_cells;
}

TEST(default_configuration_preserves_existing_uptake) {
    float consumed = run_single_cell_uptake(1.0f, NULL);
    ASSERT_NEAR(consumed, 0.05f, 0.0001f);
}

TEST(monod_uptake_low_mid_high_substrate_regimes) {
    MonodKineticsConfig config = {
        .enabled = true,
        .half_saturation = 0.2f,
        .uptake_min = 0.0f,
        .uptake_max = 1.0f,
        .growth_coupling = 0.0f,
    };

    float low = run_single_cell_uptake(0.05f, &config);
    float mid = run_single_cell_uptake(0.2f, &config);
    float high = run_single_cell_uptake(1.0f, &config);

    ASSERT(low < mid, "low substrate should consume less than mid");
    ASSERT(mid < high, "mid substrate should consume less than high");
    ASSERT_NEAR(low, 0.01f, 0.001f);
    ASSERT_NEAR(mid, 0.025f, 0.001f);
    ASSERT_NEAR(high, 0.041666f, 0.002f);
}

TEST(monod_shows_saturation_diminishing_returns) {
    MonodKineticsConfig config = {
        .enabled = true,
        .half_saturation = 0.2f,
        .uptake_min = 0.0f,
        .uptake_max = 1.0f,
        .growth_coupling = 0.0f,
    };

    float s01 = run_single_cell_uptake(0.1f, &config);
    float s02 = run_single_cell_uptake(0.2f, &config);
    float s09 = run_single_cell_uptake(0.9f, &config);
    float s10 = run_single_cell_uptake(1.0f, &config);

    float low_regime_gain = s02 - s01;
    float high_regime_gain = s10 - s09;

    ASSERT(low_regime_gain > high_regime_gain, "uptake should saturate at high substrate");
}

TEST(monod_growth_coupling_increases_spread_in_rich_substrate) {
    MonodKineticsConfig config = {
        .enabled = true,
        .half_saturation = 0.2f,
        .uptake_min = 0.0f,
        .uptake_max = 1.0f,
        .growth_coupling = 1.0f,
    };

    int low_cells = run_spread_steps(0.05f, &config, 8, 9876);
    int high_cells = run_spread_steps(1.0f, &config, 8, 9876);

    ASSERT(low_cells > 0, "low substrate run should remain viable");
    ASSERT(high_cells > low_cells, "high substrate should grow faster with coupling enabled");
}

int run_monod_kinetics_tests(void) {
    printf("\n=== Monod Kinetics Tests ===\n");

    RUN_TEST(default_configuration_preserves_existing_uptake);
    RUN_TEST(monod_uptake_low_mid_high_substrate_regimes);
    RUN_TEST(monod_shows_saturation_diminishing_returns);
    RUN_TEST(monod_growth_coupling_increases_spread_in_rich_substrate);

    printf("\nMonod Kinetics Tests: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}

int main(void) {
    return run_monod_kinetics_tests() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
