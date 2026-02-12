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
    g.mutation_rate = mutation;
    g.aggression = aggr;
    g.resilience = resil;
    g.metabolism = metab;
    // Social traits - use spread as base value to ensure distance calculations work
    g.detection_range = spread;  // 0-1 range
    g.social_factor = spread * 2.0f - 1.0f;  // Convert 0-1 to -1 to 1
    g.merge_affinity = spread;  // 0-1 range
    g.max_tracked = (uint8_t)(spread * 4.0f);  // 0-4 range
    return g;
}

// ============================================================================
// World Tests
// ============================================================================

TEST(world_create_destroy) {
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

TEST(world_create_invalid) {
    World* world = world_create(-1, 10);
    ASSERT_NULL(world);
    
    world = world_create(10, 0);
    ASSERT_NULL(world);
}

TEST(world_get_cell) {
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

TEST(world_add_colony) {
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

TEST(world_remove_colony) {
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

TEST(world_init_random_colonies) {
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

TEST(genome_create_random_valid_ranges) {
    for (int i = 0; i < 100; i++) {
        Genome g = genome_create_random();
        ASSERT_TRUE(g.spread_rate >= 0.0f && g.spread_rate <= 1.0f);
        ASSERT_TRUE(g.mutation_rate >= 0.0f && g.mutation_rate <= 0.1f);  // mutation_rate is 0-0.1
        ASSERT_TRUE(g.aggression >= 0.0f && g.aggression <= 1.0f);
        ASSERT_TRUE(g.resilience >= 0.0f && g.resilience <= 1.0f);
        ASSERT_TRUE(g.metabolism >= 0.0f && g.metabolism <= 1.0f);
    }
}

TEST(genome_mutate_changes_values) {
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

TEST(genome_mutate_stays_in_range) {
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

TEST(genome_distance_identical) {
    Genome a = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    Genome b = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    
    float dist = genome_distance(&a, &b);
    ASSERT_FLOAT_EQ(dist, 0.0f, 0.0001f);
}

TEST(genome_distance_max) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    
    float dist = genome_distance(&a, &b);
    ASSERT_FLOAT_EQ(dist, 1.0f, 0.0001f);
}

TEST(genome_distance_partial) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    
    float dist = genome_distance(&a, &b);
    ASSERT_FLOAT_EQ(dist, 0.5f, 0.0001f);
}

TEST(genome_merge_equal_weights) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    
    Genome result = genome_merge(&a, 50, &b, 50);
    
    ASSERT_FLOAT_EQ(result.spread_rate, 0.5f, 0.0001f);
    ASSERT_FLOAT_EQ(result.mutation_rate, 0.5f, 0.0001f);
    ASSERT_FLOAT_EQ(result.aggression, 0.5f, 0.0001f);
    ASSERT_FLOAT_EQ(result.resilience, 0.5f, 0.0001f);
    ASSERT_FLOAT_EQ(result.metabolism, 0.5f, 0.0001f);
}

TEST(genome_merge_weighted) {
    Genome a = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Genome b = create_test_genome(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    
    // a has 75% weight, b has 25%
    Genome result = genome_merge(&a, 75, &b, 25);
    
    ASSERT_FLOAT_EQ(result.spread_rate, 0.25f, 0.0001f);
    ASSERT_FLOAT_EQ(result.mutation_rate, 0.25f, 0.0001f);
}

TEST(genome_compatible) {
    Genome a = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    Genome b = create_test_genome(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    
    ASSERT_TRUE(genome_compatible(&a, &b, 0.1f));
    
    Genome c = create_test_genome(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    ASSERT_FALSE(genome_compatible(&a, &c, 0.1f));
}

// ============================================================================
// Simulation Tests
// ============================================================================

TEST(find_connected_components_single) {
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

TEST(find_connected_components_multiple) {
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

TEST(simulation_spread_expands) {
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

TEST(simulation_tick_advances) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    ASSERT_EQ(world->tick, 0);
    
    simulation_tick(world);
    ASSERT_EQ(world->tick, 1);
    
    simulation_tick(world);
    ASSERT_EQ(world->tick, 2);
    
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
    RUN_TEST(world_create_destroy);
    RUN_TEST(world_create_invalid);
    RUN_TEST(world_get_cell);
    RUN_TEST(world_add_colony);
    RUN_TEST(world_remove_colony);
    RUN_TEST(world_init_random_colonies);
    
    printf("\nGenetics Tests:\n");
    RUN_TEST(genome_create_random_valid_ranges);
    RUN_TEST(genome_mutate_changes_values);
    RUN_TEST(genome_mutate_stays_in_range);
    RUN_TEST(genome_distance_identical);
    RUN_TEST(genome_distance_max);
    RUN_TEST(genome_distance_partial);
    RUN_TEST(genome_merge_equal_weights);
    RUN_TEST(genome_merge_weighted);
    RUN_TEST(genome_compatible);
    
    printf("\nSimulation Tests:\n");
    RUN_TEST(find_connected_components_single);
    RUN_TEST(find_connected_components_multiple);
    RUN_TEST(simulation_spread_expands);
    RUN_TEST(simulation_tick_advances);
    
    printf("\n=====================\n");
    printf("All tests passed!\n");
    
    return 0;
}
