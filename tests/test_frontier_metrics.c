#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/server/frontier_metrics.h"
#include "../src/server/simulation.h"
#include "../src/server/world.h"
#include "../src/shared/utils.h"

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

#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) <= (eps), #a " ~= " #b)

typedef struct {
    uint32_t sectors[64];
    float diversity[64];
    float entropy[64];
} TelemetrySeries;

static Colony make_colony(uint32_t parent_id) {
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.parent_id = parent_id;
    colony.active = true;
    return colony;
}

TEST(frontier_fixture_reports_expected_diversity_and_entropy) {
    World* world = world_create(3, 1);
    ASSERT(world != NULL, "world created");

    uint32_t a = world_add_colony(world, make_colony(0));
    uint32_t b = world_add_colony(world, make_colony(0));
    ASSERT(a != 0 && b != 0, "colonies created");

    world->cells[0].colony_id = a;
    world->cells[1].colony_id = b;

    FrontierTelemetry sample;
    ASSERT(frontier_telemetry_compute(world, 77, &sample), "telemetry computed");

    ASSERT_EQ(sample.seed, 77u);
    ASSERT_EQ(sample.tick, 0u);
    ASSERT_EQ(sample.occupied_cells, 2u);
    ASSERT_EQ(sample.frontier_cells, 2u);
    ASSERT_EQ(sample.active_lineages, 2u);
    ASSERT_EQ(sample.frontier_sector_count, 2u);
    ASSERT_NEAR(sample.lineage_diversity_proxy, 0.5f, 0.0001f);
    ASSERT_NEAR(sample.lineage_entropy_bits, 1.0f, 0.0001f);

    world_destroy(world);
}

TEST(lineage_entropy_collapses_when_frontier_is_single_root) {
    World* world = world_create(4, 1);
    ASSERT(world != NULL, "world created");

    uint32_t root = world_add_colony(world, make_colony(0));
    uint32_t child_a = world_add_colony(world, make_colony(root));
    uint32_t child_b = world_add_colony(world, make_colony(root));
    ASSERT(root != 0 && child_a != 0 && child_b != 0, "lineage tree created");

    world->cells[0].colony_id = child_a;
    world->cells[1].colony_id = child_b;

    FrontierTelemetry sample;
    ASSERT(frontier_telemetry_compute(world, 99, &sample), "telemetry computed");

    ASSERT_EQ(sample.active_lineages, 1u);
    ASSERT_NEAR(sample.lineage_diversity_proxy, 0.0f, 0.0001f);
    ASSERT_NEAR(sample.lineage_entropy_bits, 0.0f, 0.0001f);

    world_destroy(world);
}

static int collect_seeded_series(uint32_t seed, TelemetrySeries* out) {
    World* world = world_create(64, 40);
    if (!world) {
        return 0;
    }

    srand(seed);
    rng_seed(seed);
    world_init_random_colonies(world, 12);

    for (int tick = 0; tick < 64; tick++) {
        simulation_tick(world);

        FrontierTelemetry sample;
        if (!frontier_telemetry_compute(world, seed, &sample)) {
            world_destroy(world);
            return 0;
        }

        out->sectors[tick] = sample.frontier_sector_count;
        out->diversity[tick] = sample.lineage_diversity_proxy;
        out->entropy[tick] = sample.lineage_entropy_bits;

        if (!(sample.lineage_diversity_proxy >= 0.0f && sample.lineage_diversity_proxy <= 1.0f)) {
            world_destroy(world);
            return 0;
        }
        if (!(sample.lineage_entropy_bits >= 0.0f)) {
            world_destroy(world);
            return 0;
        }
    }

    world_destroy(world);
    return 1;
}

TEST(seed_replay_produces_stable_telemetry_series) {
    TelemetrySeries a;
    TelemetrySeries b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    ASSERT(collect_seeded_series(4242u, &a), "first run collected");
    ASSERT(collect_seeded_series(4242u, &b), "second run collected");

    for (int i = 0; i < 64; i++) {
        ASSERT_EQ(a.sectors[i], b.sectors[i]);
        ASSERT_NEAR(a.diversity[i], b.diversity[i], 0.0001f);
        ASSERT_NEAR(a.entropy[i], b.entropy[i], 0.0001f);
    }
}

int run_frontier_metrics_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("\n=== Frontier Metrics Tests ===\n");

    RUN_TEST(frontier_fixture_reports_expected_diversity_and_entropy);
    RUN_TEST(lineage_entropy_collapses_when_frontier_is_single_root);
    RUN_TEST(seed_replay_produces_stable_telemetry_series);

    printf("\nFrontier Metrics Tests: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}

int main(void) {
    return run_frontier_metrics_tests() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
