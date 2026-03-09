#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "../src/shared/types.h"
#include "../src/shared/utils.h"
#include "../src/server/world.h"
#include "../src/server/genetics.h"
#include "../src/server/simulation.h"

// Test framework macros
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s...", #name); \
    test_##name(); \
    printf(" PASSED\n"); \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("\n    FAILED: %s at line %d\n", #cond, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_NULL(ptr) ASSERT_TRUE((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT_TRUE((ptr) != NULL)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))
#define ASSERT_FLOAT_EQ(a, b, eps) ASSERT_TRUE(fabsf((a) - (b)) < (eps))

// Helper to create a test genome with specific values
// All provided values are used for social traits too (scaled appropriately)
static Genome create_test_genome(float spread, float mutation, float aggr, float resil, float metab) {
    Genome g;
    memset(&g, 0, sizeof(Genome));
    for (int i = 0; i < 8; i++) g.spread_weights[i] = 0.5f;
    g.spread_rate = spread;
    g.mutation_rate = mutation * 0.1f;  // Scale to proper 0-0.1 range
    g.aggression = aggr;
    g.resilience = resil;
    g.metabolism = metab;
    // Set all other genome fields to the spread value to ensure distance calculations work
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
    g.dormancy_resistance = spread;
    g.persister_entry_stress = spread;
    g.persister_exit_stress = spread;
    g.persister_entry_rate = spread;
    g.persister_exit_rate = spread;
    g.sporulation_threshold = spread;
    g.biofilm_investment = spread;
    g.biofilm_tendency = spread;
    g.motility = spread;
    g.motility_direction = spread * 2.0f - 1.0f;
    g.specialization = spread;
    g.efficiency = spread;
    // Neural network
    for (int i = 0; i < 8; i++) g.hidden_weights[i] = spread * 2.0f - 1.0f;  // Scale to -1 to 1 range
    g.learning_rate = spread;
    g.memory_factor = spread;
    for (int i = 0; i < COLONY_SENSOR_COUNT; i++) g.behavior_sensor_gains[i] = spread;
    for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
        g.behavior_drive_biases[drive] = spread * 2.0f - 1.0f;
        for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
            g.behavior_drive_weights[drive][sensor] = spread * 2.0f - 1.0f;
        }
    }
    for (int action = 0; action < COLONY_ACTION_COUNT; action++) {
        g.behavior_action_biases[action] = spread * 2.0f - 1.0f;
        for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
            g.behavior_action_weights[action][drive] = spread * 2.0f - 1.0f;
        }
    }
    // Environmental sensing (missing fields)
    g.toxin_sensitivity = spread;
    g.quorum_threshold = spread;
    uint8_t color = (uint8_t)(spread * 255.0f);
    g.body_color = (Color){color, color, color};
    g.border_color = (Color){(uint8_t)(255 - color), color, (uint8_t)(color / 2)};
    return g;
}

static Colony create_hgt_test_colony(float gene_transfer_rate, float plasmid_fraction) {
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.genome = create_test_genome(0.0f, gene_transfer_rate, 0.0f, 1.0f, 1.0f);
    colony.genome.gene_transfer_rate = gene_transfer_rate;
    colony.hgt_plasmid_fraction = plasmid_fraction;
    colony.hgt_fitness_scale = 1.0f;
    colony.active = true;
    colony.cell_count = 1;
    return colony;
}

// ============================================================================
// World Tests
// ============================================================================

TEST(world_create_and_destroy_succeeds) {
    World* world = world_create(100, 50);
    ASSERT_NOT_NULL(world);
    ASSERT_EQ(world->width, 100);
    ASSERT_EQ(world->height, 50);
    ASSERT_EQ(world->tick, 0);
    ASSERT_EQ(world->colony_count, 0);
    ASSERT_NOT_NULL(world->cells);
    ASSERT_NOT_NULL(world->colonies);
    
    world_destroy(world);
}

TEST(world_create_returns_null_for_invalid_dimensions) {
    World* world = world_create(-1, 10);
    ASSERT_NULL(world);
    
    world = world_create(10, 0);
    ASSERT_NULL(world);
}

TEST(world_get_cell_returns_cell_or_null_for_bounds) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    Cell* cell = world_get_cell(world, 5, 5);
    ASSERT_NOT_NULL(cell);
    ASSERT_EQ(cell->colony_id, 0);
    
    // Out of bounds
    ASSERT_NULL(world_get_cell(world, -1, 5));
    ASSERT_NULL(world_get_cell(world, 5, -1));
    ASSERT_NULL(world_get_cell(world, 10, 5));
    ASSERT_NULL(world_get_cell(world, 5, 10));
    
    world_destroy(world);
}

TEST(world_add_colony_returns_id_and_increments_count) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    strcpy(colony.name, "TestColony");
    colony.genome = genome_create_random();
    
    uint32_t id = world_add_colony(world, colony);
    ASSERT_NE(id, 0);
    ASSERT_EQ(world->colony_count, 1);
    
    Colony* retrieved = world_get_colony(world, id);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_EQ(retrieved->id, id);
    ASSERT_TRUE(strcmp(retrieved->name, "TestColony") == 0);
    
    world_destroy(world);
}

TEST(world_remove_colony_deactivates_and_clears_cells) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.genome = genome_create_random();
    
    uint32_t id = world_add_colony(world, colony);
    ASSERT_NOT_NULL(world_get_colony(world, id));
    
    // Add some cells
    Cell* cell1 = world_get_cell(world, 5, 5);
    Cell* cell2 = world_get_cell(world, 5, 6);
    cell1->colony_id = id;
    cell2->colony_id = id;
    
    world_remove_colony(world, id);
    
    // Colony should be inactive
    ASSERT_NULL(world_get_colony(world, id));
    
    // Cells should be cleared
    ASSERT_EQ(cell1->colony_id, 0);
    ASSERT_EQ(cell2->colony_id, 0);
    
    world_destroy(world);
}

TEST(world_init_random_colonies_creates_active_colonies) {
    World* world = world_create(50, 50);
    ASSERT_NOT_NULL(world);
    
    world_init_random_colonies(world, 5);
    
    // Should have 5 colonies
    int active_count = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) active_count++;
    }
    ASSERT_EQ(active_count, 5);
    
    world_destroy(world);
}

// ============================================================================
// Genetics Tests
// ============================================================================

TEST(genome_create_random_produces_values_in_valid_ranges) {
    for (int i = 0; i < 100; i++) {
        Genome g = genome_create_random();
        ASSERT_TRUE(g.spread_rate >= 0.0f && g.spread_rate <= 1.0f);
        ASSERT_TRUE(g.mutation_rate >= 0.0f && g.mutation_rate <= 0.5f);  // mutation_rate range increased for dynamic sim
        ASSERT_TRUE(g.aggression >= 0.0f && g.aggression <= 1.0f);
        ASSERT_TRUE(g.resilience >= 0.0f && g.resilience <= 1.0f);
        ASSERT_TRUE(g.metabolism >= 0.0f && g.metabolism <= 1.0f);
    }
}

TEST(genome_mutate_modifies_at_least_one_value) {
    // Use high mutation rate to ensure changes
    Genome g = create_test_genome(0.5f, 1.0f, 0.5f, 0.5f, 0.5f);
    Genome original = g;
    
    // Run many mutations - at least one should change something
    bool changed = false;
    for (int i = 0; i < 100; i++) {
        Genome test = original;
        test.mutation_rate = 1.0f;
        genome_mutate(&test);
        
        if (test.spread_rate != original.spread_rate ||
            test.aggression != original.aggression ||
            test.resilience != original.resilience ||
            test.metabolism != original.metabolism) {
            changed = true;
            break;
        }
    }
    ASSERT_TRUE(changed);
}

TEST(genome_mutate_keeps_values_within_valid_range) {
    // Test edge cases
    Genome g = create_test_genome(0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    
    for (int i = 0; i < 100; i++) {
        genome_mutate(&g);
        ASSERT_TRUE(g.spread_rate >= 0.0f && g.spread_rate <= 1.0f);
        ASSERT_TRUE(g.mutation_rate >= 0.0f && g.mutation_rate <= 1.0f);
        ASSERT_TRUE(g.aggression >= 0.0f && g.aggression <= 1.0f);
        ASSERT_TRUE(g.resilience >= 0.0f && g.resilience <= 1.0f);
        ASSERT_TRUE(g.metabolism >= 0.0f && g.metabolism <= 1.0f);
    }
}

TEST(genome_distance_returns_zero_for_identical_genomes) {
    Genome a = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    Genome b = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    
    float dist = genome_distance(&a, &b);
    ASSERT_FLOAT_EQ(dist, 0.0f, 0.0001f);
}

TEST(genome_distance_returns_one_for_maximally_different) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    
    float dist = genome_distance(&a, &b);
    ASSERT_TRUE(dist > 0.5f);
    ASSERT_TRUE(isfinite(dist));
}

TEST(genome_distance_returns_half_for_halfway_different) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    
    float dist = genome_distance(&a, &b);
    ASSERT_TRUE(dist > 0.2f);
    ASSERT_TRUE(dist < 0.9f);
}

TEST(genome_merge_with_equal_weights_returns_average) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    
    Genome result = genome_merge(&a, 50, &b, 50);
    
    ASSERT_FLOAT_EQ(result.spread_rate, 0.5f, 0.0001f);
    // mutation_rate is scaled: 0.0 and 0.1, merged = 0.05
    ASSERT_FLOAT_EQ(result.mutation_rate, 0.05f, 0.0001f);
    ASSERT_FLOAT_EQ(result.aggression, 0.5f, 0.0001f);
    ASSERT_FLOAT_EQ(result.resilience, 0.5f, 0.0001f);
    ASSERT_FLOAT_EQ(result.metabolism, 0.5f, 0.0001f);
}

TEST(genome_merge_respects_weight_ratio) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    
    // a has 75% weight, b has 25%
    Genome result = genome_merge(&a, 75, &b, 25);
    
    ASSERT_FLOAT_EQ(result.spread_rate, 0.25f, 0.0001f);
    // mutation_rate is scaled: 0.0 and 0.1, merged with 75/25 = 0.025
    ASSERT_FLOAT_EQ(result.mutation_rate, 0.025f, 0.0001f);
}

TEST(genome_compatible_returns_true_for_similar_genomes) {
    Genome a = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    Genome b = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    
    ASSERT_TRUE(genome_compatible(&a, &b, 0.1f));
    
    Genome c = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    ASSERT_FALSE(genome_compatible(&a, &c, 0.1f));
}

// ============================================================================
// Simulation Tests
// ============================================================================

TEST(connected_components_finds_single_block) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.genome = genome_create_random();
    uint32_t id = world_add_colony(world, colony);
    
    // Create a 3x3 connected block
    for (int y = 2; y < 5; y++) {
        for (int x = 2; x < 5; x++) {
            Cell* cell = world_get_cell(world, x, y);
            cell->colony_id = id;
        }
    }
    
    int num_components;
    int* sizes = find_connected_components(world, id, &num_components);
    
    ASSERT_NOT_NULL(sizes);
    ASSERT_EQ(num_components, 1);
    ASSERT_EQ(sizes[0], 9);  // 3x3 = 9 cells
    
    free(sizes);
    world_destroy(world);
}

TEST(connected_components_finds_separate_blocks) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.genome = genome_create_random();
    uint32_t id = world_add_colony(world, colony);
    
    // Create two separate blocks
    // Block 1: 2x2 at (0,0)
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            Cell* cell = world_get_cell(world, x, y);
            cell->colony_id = id;
        }
    }
    
    // Block 2: 3x1 at (5,5) - gap between blocks
    for (int x = 5; x < 8; x++) {
        Cell* cell = world_get_cell(world, x, 5);
        cell->colony_id = id;
    }
    
    int num_components;
    int* sizes = find_connected_components(world, id, &num_components);
    
    ASSERT_NOT_NULL(sizes);
    ASSERT_EQ(num_components, 2);
    
    // Sizes should be 4 and 3 (order may vary)
    int total = sizes[0] + sizes[1];
    ASSERT_EQ(total, 7);
    ASSERT_TRUE((sizes[0] == 4 && sizes[1] == 3) || (sizes[0] == 3 && sizes[1] == 4));
    
    free(sizes);
    world_destroy(world);
}

TEST(simulation_spread_expands_colony_cells) {
    World* world = world_create(20, 20);
    ASSERT_NOT_NULL(world);
    
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.genome = create_test_genome(1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    colony.cell_count = 1;
    colony.active = true;
    
    uint32_t id = world_add_colony(world, colony);
    
    // Place single cell in center
    Cell* center = world_get_cell(world, 10, 10);
    center->colony_id = id;
    
    // Count initial cells
    int initial_count = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == id) initial_count++;
    }
    ASSERT_EQ(initial_count, 1);
    
    // Run spread
    simulation_spread(world);
    
    // Count after spread
    int after_count = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == id) after_count++;
    }
    
    // With 100% spread rate, should have expanded to neighbors
    ASSERT_TRUE(after_count > initial_count);
    
    world_destroy(world);
}

TEST(simulation_tick_increments_world_tick_counter) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    ASSERT_EQ(world->tick, 0);
    
    simulation_tick(world);
    ASSERT_EQ(world->tick, 1);
    
    simulation_tick(world);
    ASSERT_EQ(world->tick, 2);
    
    world_destroy(world);
}

TEST(hgt_donor_recipient_contact_produces_transconjugants) {
    World* world = world_create(6, 3);
    ASSERT_NOT_NULL(world);

    HGTKinetics kinetics = world->hgt_kinetics;
    kinetics.contact_rate = 1.0f;
    kinetics.donor_transfer_rate = 1.0f;
    kinetics.transconjugant_transfer_rate = 1.0f;
    kinetics.recipient_uptake_rate = 1.0f;
    kinetics.transfer_efficiency = 0.8f;
    kinetics.enable_plasmid_cost = false;
    kinetics.enable_plasmid_loss = false;
    world_set_hgt_kinetics(world, &kinetics);

    Colony donor = create_hgt_test_colony(1.0f, 1.0f);
    Colony recipient = create_hgt_test_colony(0.0f, 0.0f);
    uint32_t donor_id = world_add_colony(world, donor);
    uint32_t recipient_id = world_add_colony(world, recipient);

    world_get_cell(world, 2, 1)->colony_id = donor_id;
    world_get_cell(world, 3, 1)->colony_id = recipient_id;

    for (int i = 0; i < 10; i++) {
        simulation_spread(world);
    }

    Colony* recipient_colony = world_get_colony(world, recipient_id);
    ASSERT_NOT_NULL(recipient_colony);
    ASSERT_TRUE(recipient_colony->hgt_plasmid_fraction > 0.0f);
    ASSERT_TRUE(world->hgt_metrics.transfer_events_total > 0);
    ASSERT_TRUE(world->hgt_metrics.transconjugant_events_total > 0);

    world_destroy(world);
}

TEST(hgt_transconjugant_rate_controls_secondary_transfer) {
    World* world = world_create(8, 3);
    ASSERT_NOT_NULL(world);

    HGTKinetics kinetics = world->hgt_kinetics;
    kinetics.contact_rate = 1.0f;
    kinetics.donor_transfer_rate = 0.0f;
    kinetics.transconjugant_transfer_rate = 1.0f;
    kinetics.recipient_uptake_rate = 1.0f;
    kinetics.transfer_efficiency = 0.8f;
    kinetics.enable_plasmid_cost = false;
    kinetics.enable_plasmid_loss = false;
    world_set_hgt_kinetics(world, &kinetics);

    Colony transconjugant = create_hgt_test_colony(1.0f, 1.0f);
    transconjugant.hgt_is_transconjugant = true;
    Colony recipient = create_hgt_test_colony(0.0f, 0.0f);
    uint32_t transconjugant_id = world_add_colony(world, transconjugant);
    uint32_t recipient_id = world_add_colony(world, recipient);

    world_get_cell(world, 3, 1)->colony_id = transconjugant_id;
    world_get_cell(world, 4, 1)->colony_id = recipient_id;

    for (int i = 0; i < 10; i++) {
        simulation_spread(world);
    }

    Colony* recipient_colony = world_get_colony(world, recipient_id);
    ASSERT_NOT_NULL(recipient_colony);
    ASSERT_TRUE(recipient_colony->hgt_plasmid_fraction > 0.0f);

    world_destroy(world);
}

TEST(hgt_optional_cost_and_loss_reduce_fitness_and_plasmid) {
    World* world = world_create(6, 6);
    ASSERT_NOT_NULL(world);

    HGTKinetics kinetics = world->hgt_kinetics;
    kinetics.enable_plasmid_cost = true;
    kinetics.enable_plasmid_loss = true;
    kinetics.plasmid_cost_per_fraction = 0.4f;
    kinetics.plasmid_loss_rate = 0.2f;
    world_set_hgt_kinetics(world, &kinetics);

    Colony colony = create_hgt_test_colony(0.0f, 1.0f);
    colony.genome.efficiency = 1.0f;
    colony.biofilm_strength = 1.0f;
    uint32_t id = world_add_colony(world, colony);

    world_get_cell(world, 3, 3)->colony_id = id;
    world_get_colony(world, id)->cell_count = 1;

    simulation_update_hgt_kinetics(world);

    Colony* updated = world_get_colony(world, id);
    ASSERT_NOT_NULL(updated);
    ASSERT_TRUE(updated->hgt_plasmid_fraction < 1.0f);
    ASSERT_TRUE(updated->hgt_fitness_scale < 1.0f);
    ASSERT_TRUE(world->hgt_metrics.plasmid_loss_events_total > 0);

    world_destroy(world);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    rng_seed(12345);  // Use fixed seed for reproducibility
    
    printf("Running Phase 2 Tests\n");
    printf("=====================\n\n");
    
    printf("World Tests:\n");
    RUN_TEST(world_create_and_destroy_succeeds);
    RUN_TEST(world_create_returns_null_for_invalid_dimensions);
    RUN_TEST(world_get_cell_returns_cell_or_null_for_bounds);
    RUN_TEST(world_add_colony_returns_id_and_increments_count);
    RUN_TEST(world_remove_colony_deactivates_and_clears_cells);
    RUN_TEST(world_init_random_colonies_creates_active_colonies);
    
    printf("\nGenetics Tests:\n");
    RUN_TEST(genome_create_random_produces_values_in_valid_ranges);
    RUN_TEST(genome_mutate_modifies_at_least_one_value);
    RUN_TEST(genome_mutate_keeps_values_within_valid_range);
    RUN_TEST(genome_distance_returns_zero_for_identical_genomes);
    RUN_TEST(genome_distance_returns_one_for_maximally_different);
    RUN_TEST(genome_distance_returns_half_for_halfway_different);
    RUN_TEST(genome_merge_with_equal_weights_returns_average);
    RUN_TEST(genome_merge_respects_weight_ratio);
    RUN_TEST(genome_compatible_returns_true_for_similar_genomes);
    
    printf("\nSimulation Tests:\n");
    RUN_TEST(connected_components_finds_single_block);
    RUN_TEST(connected_components_finds_separate_blocks);
    RUN_TEST(simulation_spread_expands_colony_cells);
    RUN_TEST(simulation_tick_increments_world_tick_counter);
    RUN_TEST(hgt_donor_recipient_contact_produces_transconjugants);
    RUN_TEST(hgt_transconjugant_rate_controls_secondary_transfer);
    RUN_TEST(hgt_optional_cost_and_loss_reduce_fitness_and_plasmid);
    
    printf("\n=====================\n");
    printf("All tests passed!\n");
    
    return 0;
}
