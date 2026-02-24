#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/shared/types.h"
#include "../src/server/world.h"
#include "../src/server/simulation_common.h"

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

static Colony make_colony(uint32_t id) {
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.id = id;
    colony.active = true;
    return colony;
}

TEST(calculate_local_density_counts_valid_neighbors) {
    World* world = world_create(5, 5);
    ASSERT(world != NULL, "world created");

    world->cells[2 * world->width + 2].colony_id = 7;
    world->cells[2 * world->width + 3].colony_id = 7;
    world->cells[3 * world->width + 2].colony_id = 9;

    float density = calculate_local_density(world, 0, 0, 7);
    ASSERT_NEAR(density, 2.0f / 16.0f, 0.0001f);

    world_destroy(world);
}

TEST(calculate_env_spread_modifier_applies_density_penalty) {
    World* world = world_create(7, 7);
    ASSERT(world != NULL, "world created");

    Colony colony = make_colony(3);
    colony.genome.nutrient_sensitivity = 0.0f;
    colony.genome.toxin_sensitivity = 0.0f;
    colony.genome.edge_affinity = 0.0f;
    colony.genome.quorum_threshold = 0.20f;
    colony.genome.density_tolerance = 0.0f;

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int idx = y * world->width + x;
            world->nutrients[idx] = 0.5f;
            world->toxins[idx] = 0.0f;
            world->cells[idx].colony_id = 3;
        }
    }

    float modifier = calculate_env_spread_modifier(world, &colony, 3, 3, 3, 3);
    ASSERT_NEAR(modifier, 0.3f, 0.0001f);

    world_destroy(world);
}

TEST(calculate_env_spread_modifier_clamps_high_values) {
    World* world = world_create(7, 7);
    ASSERT(world != NULL, "world created");

    Colony colony = make_colony(4);
    colony.genome.nutrient_sensitivity = 4.0f;
    colony.genome.toxin_sensitivity = 0.0f;
    colony.genome.edge_affinity = 0.0f;
    colony.genome.quorum_threshold = 1.0f;
    colony.genome.density_tolerance = 1.0f;

    int idx = 3 * world->width + 3;
    world->nutrients[idx] = 1.0f;
    world->toxins[idx] = 0.0f;

    float modifier = calculate_env_spread_modifier(world, &colony, 3, 3, 3, 3);
    ASSERT_NEAR(modifier, 2.0f, 0.0001f);

    world_destroy(world);
}

TEST(calculate_biomass_pressure_handles_isolated_world) {
    World* world = world_create(1, 1);
    ASSERT(world != NULL, "world created");

    float pressure = calculate_biomass_pressure(world, 0, 0, 1);
    ASSERT_NEAR(pressure, 1.0f, 0.0001f);

    world_destroy(world);
}

TEST(calculate_biomass_pressure_scales_with_friendly_neighbors) {
    World* world = world_create(3, 3);
    ASSERT(world != NULL, "world created");

    world_get_cell(world, 0, 0)->colony_id = 8;
    world_get_cell(world, 1, 0)->colony_id = 8;
    world_get_cell(world, 2, 0)->colony_id = 8;
    world_get_cell(world, 0, 1)->colony_id = 8;
    world_get_cell(world, 2, 1)->colony_id = 8;
    world_get_cell(world, 0, 2)->colony_id = 0;
    world_get_cell(world, 1, 2)->colony_id = 2;
    world_get_cell(world, 2, 2)->colony_id = 0;

    float pressure = calculate_biomass_pressure(world, 1, 1, 8);
    ASSERT_NEAR(pressure, 1.3125f, 0.0001f);

    world_destroy(world);
}

TEST(get_quorum_activation_handles_null_below_and_above_threshold) {
    ASSERT_NEAR(get_quorum_activation(NULL), 0.0f, 0.0001f);

    Colony colony = make_colony(10);
    colony.genome.quorum_threshold = 0.4f;
    colony.signal_strength = 0.4f;
    ASSERT_NEAR(get_quorum_activation(&colony), 0.0f, 0.0001f);

    colony.signal_strength = 0.8f;
    ASSERT(get_quorum_activation(&colony) > 0.6f, "activation increases above threshold");
}

TEST(expensive_trait_load_reflects_configured_weights) {
    Genome g;
    memset(&g, 0, sizeof(Genome));

    g.toxin_production = 1.0f;
    g.biofilm_investment = 1.0f;
    g.biofilm_tendency = 1.0f;
    g.signal_emission = 1.0f;
    g.motility = 1.0f;

    float load = calculate_expensive_trait_load(&g);
    ASSERT_NEAR(load, 0.78f, 0.0001f);
}

TEST(growth_and_survival_costs_create_tradeoff_for_max_traits) {
    Colony balanced = make_colony(30);
    balanced.genome.toxin_production = 0.3f;
    balanced.genome.biofilm_investment = 0.3f;
    balanced.genome.biofilm_tendency = 0.6f;
    balanced.genome.signal_emission = 0.3f;
    balanced.genome.motility = 0.3f;

    Colony maxed = make_colony(31);
    maxed.genome.toxin_production = 1.0f;
    maxed.genome.biofilm_investment = 1.0f;
    maxed.genome.biofilm_tendency = 1.0f;
    maxed.genome.signal_emission = 1.0f;
    maxed.genome.motility = 1.0f;

    float balanced_growth = calculate_growth_cost_multiplier(&balanced);
    float max_growth = calculate_growth_cost_multiplier(&maxed);
    ASSERT(max_growth < balanced_growth, "max expensive traits should reduce growth multiplier");

    float balanced_survival = calculate_survival_cost_multiplier(&balanced);
    float max_survival = calculate_survival_cost_multiplier(&maxed);
    ASSERT(max_survival > balanced_survival, "max expensive traits should increase survival burden");
}

TEST(baseline_sweep_avoids_universal_max_trait_dominance) {
    const float levels[] = {0.0f, 0.5f, 1.0f};
    float best_score = -1.0f;
    bool best_all_max = false;

    for (int ti = 0; ti < 3; ti++) {
        for (int bi = 0; bi < 3; bi++) {
            for (int si = 0; si < 3; si++) {
                for (int mi = 0; mi < 3; mi++) {
                    Colony colony = make_colony(40);
                    colony.genome.toxin_production = levels[ti];
                    colony.genome.biofilm_investment = levels[bi];
                    colony.genome.biofilm_tendency = levels[bi];
                    colony.genome.signal_emission = levels[si];
                    colony.genome.motility = levels[mi];

                    float growth = calculate_growth_cost_multiplier(&colony);
                    float survival = calculate_survival_cost_multiplier(&colony);
                    float score = growth / survival;

                    if (score > best_score) {
                        best_score = score;
                        best_all_max = (levels[ti] == 1.0f && levels[bi] == 1.0f &&
                                        levels[si] == 1.0f && levels[mi] == 1.0f);
                    }
                }
            }
        }
    }

    ASSERT(!best_all_max, "all-max expensive trait profile should not dominate baseline sweep");
}

TEST(get_direction_weight_maps_all_cardinals_and_default) {
    Genome g;
    memset(&g, 0, sizeof(Genome));
    for (int i = 0; i < 8; i++) {
        g.spread_weights[i] = (float)(i + 1) / 10.0f;
    }

    ASSERT_NEAR(get_direction_weight(&g, 0, -1), g.spread_weights[0], 0.0001f);
    ASSERT_NEAR(get_direction_weight(&g, 1, -1), g.spread_weights[1], 0.0001f);
    ASSERT_NEAR(get_direction_weight(&g, 1, 0), g.spread_weights[2], 0.0001f);
    ASSERT_NEAR(get_direction_weight(&g, 1, 1), g.spread_weights[3], 0.0001f);
    ASSERT_NEAR(get_direction_weight(&g, 0, 1), g.spread_weights[4], 0.0001f);
    ASSERT_NEAR(get_direction_weight(&g, -1, 1), g.spread_weights[5], 0.0001f);
    ASSERT_NEAR(get_direction_weight(&g, -1, 0), g.spread_weights[6], 0.0001f);
    ASSERT_NEAR(get_direction_weight(&g, -1, -1), g.spread_weights[7], 0.0001f);
    ASSERT_NEAR(get_direction_weight(&g, 2, 2), 1.0f, 0.0001f);
}

TEST(calculate_curvature_boost_reflects_neighbor_count) {
    World* world = world_create(3, 3);
    ASSERT(world != NULL, "world created");

    float base_boost = calculate_curvature_boost(world, 1, 1, 12);
    ASSERT_NEAR(base_boost, 0.85f, 0.0001f);

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            if (x == 1 && y == 1) {
                continue;
            }
            world_get_cell(world, x, y)->colony_id = 12;
        }
    }

    float full_boost = calculate_curvature_boost(world, 1, 1, 12);
    ASSERT_NEAR(full_boost, 2.05f, 0.0001f);

    world_destroy(world);
}

TEST(calculate_perception_modifier_handles_range_and_threat_branches) {
    World* world = world_create(6, 1);
    ASSERT(world != NULL, "world created");

    Colony low_range = make_colony(20);
    low_range.genome.detection_range = 0.01f;
    ASSERT_NEAR(calculate_perception_modifier(world, 1, 0, 1, 0, &low_range), 1.0f, 0.0001f);

    Colony zero_samples = make_colony(20);
    zero_samples.genome.detection_range = 0.5f;
    ASSERT_NEAR(calculate_perception_modifier(world, 0, 0, -1, 0, &zero_samples), 1.0f, 0.0001f);

    for (int x = 1; x <= 5; x++) {
        int idx = x;
        world->nutrients[idx] = 1.0f;
        world->cells[idx].colony_id = 99;
    }

    Colony aggressive = make_colony(20);
    aggressive.genome.detection_range = 0.6f;
    aggressive.genome.nutrient_sensitivity = 1.0f;
    aggressive.genome.aggression = 1.0f;
    float boosted = calculate_perception_modifier(world, 0, 0, 1, 0, &aggressive);
    ASSERT_NEAR(boosted, 1.95f, 0.0001f);

    Colony passive = aggressive;
    passive.genome.aggression = 0.0f;
    float reduced = calculate_perception_modifier(world, 0, 0, 1, 0, &passive);
    ASSERT(reduced < 1.5f, "passive colony dampens threat response");

    world_destroy(world);
}

TEST(get_scent_influence_covers_bounds_self_and_enemy_signals) {
    World* world = world_create(3, 1);
    ASSERT(world != NULL, "world created");

    Genome genome;
    memset(&genome, 0, sizeof(Genome));
    genome.density_tolerance = 0.2f;
    genome.aggression = 1.0f;
    genome.defense_priority = 0.0f;
    genome.signal_sensitivity = 1.0f;

    ASSERT_NEAR(get_scent_influence(world, 0, 0, -1, 0, 5, &genome), 1.0f, 0.0001f);

    world->signals[1] = 0.005f;
    world->signal_source[1] = 6;
    ASSERT_NEAR(get_scent_influence(world, 0, 0, 1, 0, 5, &genome), 1.0f, 0.0001f);

    world->signals[1] = 0.8f;
    world->signal_source[1] = 5;
    ASSERT_NEAR(get_scent_influence(world, 0, 0, 1, 0, 5, &genome), 0.808f, 0.0001f);

    world->signal_source[1] = 6;
    ASSERT_NEAR(get_scent_influence(world, 0, 0, 1, 0, 5, &genome), 1.4f, 0.0001f);

    genome.aggression = 0.0f;
    genome.defense_priority = 1.0f;
    ASSERT_NEAR(get_scent_influence(world, 0, 0, 1, 0, 5, &genome), 0.6f, 0.0001f);

    world_destroy(world);
}

TEST(neighbor_counters_distinguish_friendly_and_enemy) {
    World* world = world_create(3, 3);
    ASSERT(world != NULL, "world created");

    world_get_cell(world, 0, 0)->colony_id = 7;
    world_get_cell(world, 1, 0)->colony_id = 8;
    world_get_cell(world, 2, 0)->colony_id = 0;
    world_get_cell(world, 0, 1)->colony_id = 7;
    world_get_cell(world, 2, 1)->colony_id = 9;
    world_get_cell(world, 0, 2)->colony_id = 7;
    world_get_cell(world, 1, 2)->colony_id = 8;
    world_get_cell(world, 2, 2)->colony_id = 0;

    ASSERT_EQ(count_friendly_neighbors(world, 1, 1, 7), 3);
    ASSERT_EQ(count_enemy_neighbors(world, 1, 1, 7), 3);

    world_destroy(world);
}

int run_simulation_common_tests(void) {
    printf("\n=== Simulation Common Helper Tests ===\n");

    RUN_TEST(calculate_local_density_counts_valid_neighbors);
    RUN_TEST(calculate_env_spread_modifier_applies_density_penalty);
    RUN_TEST(calculate_env_spread_modifier_clamps_high_values);
    RUN_TEST(calculate_biomass_pressure_handles_isolated_world);
    RUN_TEST(calculate_biomass_pressure_scales_with_friendly_neighbors);
    RUN_TEST(get_quorum_activation_handles_null_below_and_above_threshold);
    RUN_TEST(expensive_trait_load_reflects_configured_weights);
    RUN_TEST(growth_and_survival_costs_create_tradeoff_for_max_traits);
    RUN_TEST(baseline_sweep_avoids_universal_max_trait_dominance);
    RUN_TEST(get_direction_weight_maps_all_cardinals_and_default);
    RUN_TEST(calculate_curvature_boost_reflects_neighbor_count);
    RUN_TEST(calculate_perception_modifier_handles_range_and_threat_branches);
    RUN_TEST(get_scent_influence_covers_bounds_self_and_enemy_signals);
    RUN_TEST(neighbor_counters_distinguish_friendly_and_enemy);

    printf("\nSimulation Common Tests: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}

int main(void) {
    return run_simulation_common_tests() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
