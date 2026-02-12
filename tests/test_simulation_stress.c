/**
 * test_simulation_stress.c - Stress tests for bacterial colony simulation
 * Tests long-running simulations, memory stability, and edge cases
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/shared/types.h"
#include "../src/shared/utils.h"
#include "../src/shared/names.h"
#include "../src/server/world.h"
#include "../src/server/genetics.h"
#include "../src/server/simulation.h"

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
#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_GE(a, b) ASSERT((a) >= (b), #a " >= " #b)
#define ASSERT_GT(a, b) ASSERT((a) > (b), #a " > " #b)
#define ASSERT_LE(a, b) ASSERT((a) <= (b), #a " <= " #b)

// Helper to create a colony
static Colony create_test_colony(void) {
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.id = 0;
    generate_scientific_name(colony.name, sizeof(colony.name));
    colony.genome = genome_create_random();
    colony.cell_count = 0;
    colony.active = true;
    colony.age = 0;
    colony.parent_id = 0;
    return colony;
}

// ============================================================================
// Long Running Tests
// ============================================================================

TEST(thousand_ticks_no_crash) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    world_init_random_colonies(world, 10);
    
    // Run 1000 ticks
    for (int i = 0; i < 1000; i++) {
        simulation_tick(world);
        
        // Basic sanity checks every 100 ticks
        if (i % 100 == 0) {
            ASSERT(world->tick == (uint64_t)(i + 1), "Tick counter incorrect");
        }
    }
    
    ASSERT_EQ(world->tick, 1000);
    world_destroy(world);
}

TEST(rapid_growth_death_cycles) {
    World* world = world_create(50, 50);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    
    for (int cycle = 0; cycle < 10; cycle++) {
        // Add colonies
        world_init_random_colonies(world, 5);
        
        // Run some ticks
        for (int i = 0; i < 50; i++) {
            simulation_tick(world);
        }
        
        // Remove all colonies
        for (size_t i = 0; i < world->colony_count; i++) {
            if (world->colonies[i].active) {
                world_remove_colony(world, world->colonies[i].id);
            }
        }
        
        // Verify world is clean
        int cells_remaining = 0;
        for (int i = 0; i < world->width * world->height; i++) {
            if (world->cells[i].colony_id != 0) cells_remaining++;
        }
        ASSERT_EQ(cells_remaining, 0);
    }
    
    world_destroy(world);
}

TEST(all_colonies_dying_leaves_empty_world) {
    World* world = world_create(30, 30);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    world_init_random_colonies(world, 5);
    
    // Run some ticks
    for (int i = 0; i < 20; i++) {
        simulation_tick(world);
    }
    
    // Kill all colonies
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) {
            world_remove_colony(world, world->colonies[i].id);
        }
    }
    
    // Verify world is empty
    int non_empty_cells = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id != 0) non_empty_cells++;
    }
    ASSERT_EQ(non_empty_cells, 0);
    
    // Continue ticking on empty world
    for (int i = 0; i < 100; i++) {
        simulation_tick(world);
    }
    
    world_destroy(world);
}

// ============================================================================
// Single Cell Tests
// ============================================================================

TEST(single_cell_colony_divides) {
    World* world = world_create(20, 20);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    
    Colony colony = create_test_colony();
    colony.genome.spread_rate = 1.0f;
    colony.genome.metabolism = 1.0f;
    uint32_t id = world_add_colony(world, colony);
    
    // Place single cell
    world_get_cell(world, 10, 10)->colony_id = id;
    Colony* col = world_get_colony(world, id);
    col->cell_count = 1;
    
    // Run until colony grows significantly
    for (int i = 0; i < 100; i++) {
        simulation_tick(world);
    }
    
    // Should have grown
    int cell_count = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id != 0) cell_count++;
    }
    ASSERT_GT(cell_count, 1);
    
    world_destroy(world);
}

// ============================================================================
// Large Colony Tests
// ============================================================================

TEST(large_colony_1000_cells_divides) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    Colony colony = create_test_colony();
    uint32_t id = world_add_colony(world, colony);
    
    // Create two separate blocks of cells (guaranteed to be split from start)
    // Block 1: 500 cells
    int cells_placed = 0;
    for (int y = 10; y < 30; y++) {
        for (int x = 10; x < 35; x++) {
            world_get_cell(world, x, y)->colony_id = id;
            cells_placed++;
        }
    }
    
    // Block 2: 500 cells (separate from block 1 with a gap)
    for (int y = 50; y < 70; y++) {
        for (int x = 50; x < 75; x++) {
            world_get_cell(world, x, y)->colony_id = id;
            cells_placed++;
        }
    }
    
    Colony* col = world_get_colony(world, id);
    col->cell_count = cells_placed;
    
    ASSERT_GT(cells_placed, 500);  // Should have at least 500 cells
    
    size_t initial_colony_count = world->colony_count;
    
    simulation_check_divisions(world);
    
    // Should have created new colony due to two disconnected components
    ASSERT_GT(world->colony_count, initial_colony_count);
    
    world_destroy(world);
}

TEST(large_colony_processing_time) {
    World* world = world_create(200, 200);
    ASSERT_NOT_NULL(world);
    
    Colony colony = create_test_colony();
    colony.genome.spread_rate = 0.1f;  // Slow spread
    uint32_t id = world_add_colony(world, colony);
    
    // Fill a large area
    for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
            world_get_cell(world, x, y)->colony_id = id;
        }
    }
    
    Colony* col = world_get_colony(world, id);
    col->cell_count = 10000;
    
    // Run simulation - should complete in reasonable time
    for (int i = 0; i < 10; i++) {
        simulation_tick(world);
    }
    
    ASSERT_EQ(world->tick, 10);
    
    world_destroy(world);
}

// ============================================================================
// Many Small Colonies Tests
// ============================================================================

TEST(many_small_colonies_interact) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    
    // Create 100 colonies
    for (int i = 0; i < 100; i++) {
        Colony colony = create_test_colony();
        colony.genome.spread_rate = 0.5f;
        colony.genome.metabolism = 0.5f;
        colony.genome.aggression = 0.3f;
        uint32_t id = world_add_colony(world, colony);
        
        // Place at scattered positions
        int x = (i % 10) * 10 + 5;
        int y = (i / 10) * 10 + 5;
        world_get_cell(world, x, y)->colony_id = id;
        
        Colony* col = world_get_colony(world, id);
        col->cell_count = 1;
    }
    
    ASSERT_GE(world->colony_count, 100);
    
    // Run simulation
    for (int i = 0; i < 100; i++) {
        simulation_tick(world);
    }
    
    // Simulation should complete without crash
    ASSERT_EQ(world->tick, 100);
    
    world_destroy(world);
}

TEST(concurrent_divisions_and_mergers) {
    World* world = world_create(50, 50);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    
    // Create colonies with similar genomes (compatible for merging)
    for (int i = 0; i < 20; i++) {
        Colony colony = create_test_colony();
        // Make genomes similar
        colony.genome.spread_rate = 0.5f;
        colony.genome.mutation_rate = 0.01f;
        colony.genome.aggression = 0.5f;
        colony.genome.resilience = 0.5f;
        colony.genome.metabolism = 0.5f;
        
        uint32_t id = world_add_colony(world, colony);
        
        int x = rand_range(5, 44);
        int y = rand_range(5, 44);
        world_get_cell(world, x, y)->colony_id = id;
        
        Colony* col = world_get_colony(world, id);
        col->cell_count = 1;
    }
    
    // Run simulation with many ticks
    for (int i = 0; i < 200; i++) {
        simulation_tick(world);
    }
    
    // Should complete without crash
    ASSERT_EQ(world->tick, 200);
    
    world_destroy(world);
}

// ============================================================================
// Memory Stability Tests
// ============================================================================

TEST(memory_stability_over_time) {
    // Run multiple simulation cycles to check for memory issues
    for (int cycle = 0; cycle < 5; cycle++) {
        World* world = world_create(50, 50);
        ASSERT_NOT_NULL(world);
        
        rng_seed(42 + cycle);
        world_init_random_colonies(world, 10);
        
        // Run simulation
        for (int i = 0; i < 100; i++) {
            simulation_tick(world);
        }
        
        world_destroy(world);
    }
    
    // If we get here without crash, test passed
    ASSERT_TRUE(1);
}

TEST(colony_array_growth) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    // Force many divisions to grow colony array
    for (int i = 0; i < 100; i++) {
        Colony colony = create_test_colony();
        uint32_t id = world_add_colony(world, colony);
        
        // Place disconnected cells to force divisions
        world_get_cell(world, i % 10, i / 10)->colony_id = id;
        world_get_cell(world, 50 + i % 10, i / 10)->colony_id = id;
        
        Colony* col = world_get_colony(world, id);
        col->cell_count = 2;
    }
    
    // Run divisions repeatedly
    for (int i = 0; i < 10; i++) {
        simulation_check_divisions(world);
    }
    
    ASSERT_GE(world->colony_capacity, world->colony_count);
    
    world_destroy(world);
}

TEST(world_create_destroy_many_times) {
    for (int i = 0; i < 100; i++) {
        World* world = world_create(50 + (i % 50), 50 + (i % 50));
        ASSERT_NOT_NULL(world);
        
        // Do some operations
        world_init_random_colonies(world, 5);
        simulation_tick(world);
        
        world_destroy(world);
    }
    
    // If we get here without crash, test passed
    ASSERT_TRUE(1);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(minimal_world_size) {
    World* world = world_create(1, 1);
    ASSERT_NOT_NULL(world);
    
    Colony colony = create_test_colony();
    uint32_t id = world_add_colony(world, colony);
    
    world_get_cell(world, 0, 0)->colony_id = id;
    Colony* col = world_get_colony(world, id);
    col->cell_count = 1;
    
    // Run simulation on 1x1 world
    for (int i = 0; i < 100; i++) {
        simulation_tick(world);
    }
    
    ASSERT_EQ(world->tick, 100);
    
    world_destroy(world);
}

TEST(asymmetric_world) {
    World* world = world_create(200, 5);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    world_init_random_colonies(world, 10);
    
    for (int i = 0; i < 100; i++) {
        simulation_tick(world);
    }
    
    ASSERT_EQ(world->tick, 100);
    
    world_destroy(world);
}

TEST(tall_narrow_world) {
    World* world = world_create(5, 200);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    world_init_random_colonies(world, 10);
    
    for (int i = 0; i < 100; i++) {
        simulation_tick(world);
    }
    
    ASSERT_EQ(world->tick, 100);
    
    world_destroy(world);
}

// ============================================================================
// Run Tests
// ============================================================================

int run_simulation_stress_tests(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    printf("\n=== Simulation Stress Tests ===\n\n");
    
    printf("Long Running Tests:\n");
    RUN_TEST(thousand_ticks_no_crash);
    RUN_TEST(rapid_growth_death_cycles);
    RUN_TEST(all_colonies_dying_leaves_empty_world);
    
    printf("\nSingle Cell Tests:\n");
    RUN_TEST(single_cell_colony_divides);
    
    printf("\nLarge Colony Tests:\n");
    RUN_TEST(large_colony_1000_cells_divides);
    RUN_TEST(large_colony_processing_time);
    
    printf("\nMany Small Colonies Tests:\n");
    RUN_TEST(many_small_colonies_interact);
    RUN_TEST(concurrent_divisions_and_mergers);
    
    printf("\nMemory Stability Tests:\n");
    RUN_TEST(memory_stability_over_time);
    RUN_TEST(colony_array_growth);
    RUN_TEST(world_create_destroy_many_times);
    
    printf("\nEdge Cases:\n");
    RUN_TEST(minimal_world_size);
    RUN_TEST(asymmetric_world);
    RUN_TEST(tall_narrow_world);
    
    printf("\n--- Simulation Stress Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_simulation_stress_tests() > 0 ? 1 : 0;
}
#endif
