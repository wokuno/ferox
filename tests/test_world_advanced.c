/**
 * test_world_advanced.c - Advanced world tests for bacterial colony simulator
 * Tests boundary conditions, colony management, ticking, and world operations
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
#define ASSERT_FALSE(cond) ASSERT(!(cond), "!" #cond)
#define ASSERT_NULL(ptr) ASSERT((ptr) == NULL, #ptr " is NULL")
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL, #ptr " is not NULL")
#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_NE(a, b) ASSERT((a) != (b), #a " != " #b)
#define ASSERT_LE(a, b) ASSERT((a) <= (b), #a " <= " #b)
#define ASSERT_GE(a, b) ASSERT((a) >= (b), #a " >= " #b)
#define ASSERT_LT(a, b) ASSERT((a) < (b), #a " < " #b)
#define ASSERT_GT(a, b) ASSERT((a) > (b), #a " > " #b)

// Helper to create a basic colony
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
// World Boundary Tests
// ============================================================================

TEST(world_get_cell_returns_valid_corner_cells) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    // Test all corners
    Cell* top_left = world_get_cell(world, 0, 0);
    Cell* top_right = world_get_cell(world, 9, 0);
    Cell* bottom_left = world_get_cell(world, 0, 9);
    Cell* bottom_right = world_get_cell(world, 9, 9);
    
    ASSERT_NOT_NULL(top_left);
    ASSERT_NOT_NULL(top_right);
    ASSERT_NOT_NULL(bottom_left);
    ASSERT_NOT_NULL(bottom_right);
    
    // All should be different cells
    ASSERT(top_left != top_right, "Corners should be different");
    ASSERT(top_left != bottom_left, "Corners should be different");
    ASSERT(top_left != bottom_right, "Corners should be different");
    
    world_destroy(world);
}

TEST(world_get_cell_returns_valid_edge_cells) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    // Test edge cells
    for (int i = 0; i < 10; i++) {
        // Top edge
        ASSERT_NOT_NULL(world_get_cell(world, i, 0));
        // Bottom edge
        ASSERT_NOT_NULL(world_get_cell(world, i, 9));
        // Left edge
        ASSERT_NOT_NULL(world_get_cell(world, 0, i));
        // Right edge
        ASSERT_NOT_NULL(world_get_cell(world, 9, i));
    }
    
    // Just outside bounds
    ASSERT_NULL(world_get_cell(world, -1, 0));
    ASSERT_NULL(world_get_cell(world, 10, 0));
    ASSERT_NULL(world_get_cell(world, 0, -1));
    ASSERT_NULL(world_get_cell(world, 0, 10));
    
    world_destroy(world);
}

TEST(world_boundary_cells_can_be_assigned) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    Colony colony = create_test_colony();
    uint32_t id = world_add_colony(world, colony);
    
    // Place cells at corners
    world_get_cell(world, 0, 0)->colony_id = id;
    world_get_cell(world, 9, 0)->colony_id = id;
    world_get_cell(world, 0, 9)->colony_id = id;
    world_get_cell(world, 9, 9)->colony_id = id;
    
    // Verify they're set
    ASSERT_EQ(world_get_cell(world, 0, 0)->colony_id, id);
    ASSERT_EQ(world_get_cell(world, 9, 9)->colony_id, id);
    
    world_destroy(world);
}

// ============================================================================
// Colony Removal Tests
// ============================================================================

TEST(world_remove_colony_clears_all_cells) {
    World* world = world_create(20, 20);
    ASSERT_NOT_NULL(world);
    
    Colony colony = create_test_colony();
    uint32_t id = world_add_colony(world, colony);
    
    // Spread cells throughout the world
    for (int y = 0; y < 20; y += 2) {
        for (int x = 0; x < 20; x += 2) {
            world_get_cell(world, x, y)->colony_id = id;
        }
    }
    
    // Remove colony
    world_remove_colony(world, id);
    
    // Verify all cells are cleared
    int cells_remaining = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == id) {
            cells_remaining++;
        }
    }
    
    ASSERT_EQ(cells_remaining, 0);
    ASSERT_NULL(world_get_colony(world, id));
    
    world_destroy(world);
}

TEST(world_remove_colony_preserves_other_colonies) {
    World* world = world_create(20, 20);
    ASSERT_NOT_NULL(world);
    
    Colony colony1 = create_test_colony();
    Colony colony2 = create_test_colony();
    uint32_t id1 = world_add_colony(world, colony1);
    uint32_t id2 = world_add_colony(world, colony2);
    
    // Place cells for both colonies
    for (int x = 0; x < 10; x++) {
        world_get_cell(world, x, 5)->colony_id = id1;
        world_get_cell(world, x + 10, 5)->colony_id = id2;
    }
    
    // Remove first colony
    world_remove_colony(world, id1);
    
    // Verify second colony is intact
    int colony2_cells = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == id2) {
            colony2_cells++;
        }
    }
    
    ASSERT_EQ(colony2_cells, 10);
    ASSERT_NOT_NULL(world_get_colony(world, id2));
    
    world_destroy(world);
}

// ============================================================================
// Multiple Colony Tests
// ============================================================================

TEST(world_init_colonies_do_not_overlap) {
    World* world = world_create(50, 50);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    world_init_random_colonies(world, 10);
    
    // Count cells per colony
    int total_cells = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id != 0) {
            total_cells++;
        }
    }
    
    // Each colony starts with 1 cell
    ASSERT_EQ(total_cells, 10);
    
    world_destroy(world);
}

TEST(world_supports_200_colonies) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    // Add many colonies
    for (int i = 0; i < 200; i++) {
        Colony colony = create_test_colony();
        uint32_t id = world_add_colony(world, colony);
        ASSERT_NE(id, 0);
    }
    
    ASSERT_EQ(world->colony_count, 200);
    ASSERT_GE(world->colony_capacity, 200);
    
    world_destroy(world);
}

TEST(world_assigns_unique_colony_ids) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    uint32_t ids[50];
    for (int i = 0; i < 50; i++) {
        Colony colony = create_test_colony();
        ids[i] = world_add_colony(world, colony);
    }
    
    // Check all IDs are unique
    for (int i = 0; i < 50; i++) {
        for (int j = i + 1; j < 50; j++) {
            ASSERT_NE(ids[i], ids[j]);
        }
    }
    
    world_destroy(world);
}

// ============================================================================
// World Tick Tests
// ============================================================================

TEST(simulation_tick_increases_cell_age) {
    World* world = world_create(20, 20);
    ASSERT_NOT_NULL(world);
    
    Colony colony = create_test_colony();
    uint32_t id = world_add_colony(world, colony);
    
    // Place a cell
    Cell* cell = world_get_cell(world, 10, 10);
    cell->colony_id = id;
    
    Colony* col = world_get_colony(world, id);
    col->cell_count = 1;
    
    uint8_t initial_age = cell->age;
    
    // Run tick
    simulation_tick(world);
    
    // Cell age should increase
    ASSERT_GT(cell->age, initial_age);
    
    world_destroy(world);
}

TEST(simulation_tick_increments_world_counter) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    ASSERT_EQ(world->tick, 0);
    
    for (int i = 1; i <= 10; i++) {
        simulation_tick(world);
        ASSERT_EQ(world->tick, (uint64_t)i);
    }
    
    world_destroy(world);
}

// ============================================================================
// Spreading Tests
// ============================================================================

TEST(simulation_spread_stays_within_bounds) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    Colony colony = create_test_colony();
    colony.genome.spread_rate = 1.0f;
    colony.genome.metabolism = 1.0f;
    uint32_t id = world_add_colony(world, colony);
    
    // Place cell at corner
    Cell* corner = world_get_cell(world, 0, 0);
    corner->colony_id = id;
    
    Colony* col = world_get_colony(world, id);
    col->cell_count = 1;
    
    // Run spread many times
    for (int i = 0; i < 100; i++) {
        simulation_spread(world);
    }
    
    // No cells should be outside bounds
    // (If spread went out of bounds, it would crash or corrupt memory)
    // Just verify we can still access all cells
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            Cell* c = world_get_cell(world, x, y);
            ASSERT_NOT_NULL(c);
            ASSERT(c->colony_id == 0 || c->colony_id == id, 
                   "Cell should be empty or belong to our colony");
        }
    }
    
    world_destroy(world);
}

TEST(simulation_spread_expands_from_center) {
    World* world = world_create(20, 20);
    ASSERT_NOT_NULL(world);
    
    rng_seed(42);
    
    Colony colony = create_test_colony();
    colony.genome.spread_rate = 1.0f;
    colony.genome.metabolism = 1.0f;
    uint32_t id = world_add_colony(world, colony);
    
    // Place cell in center
    world_get_cell(world, 10, 10)->colony_id = id;
    Colony* col = world_get_colony(world, id);
    col->cell_count = 1;
    
    // Run many ticks
    for (int i = 0; i < 100; i++) {
        simulation_spread(world);
    }
    
    // Should have spread significantly
    int cell_count = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == id) cell_count++;
    }
    
    ASSERT_GT(cell_count, 1);
    
    world_destroy(world);
}

// ============================================================================
// Division Tests
// ============================================================================

TEST(simulation_division_creates_new_colony) {
    World* world = world_create(20, 20);
    ASSERT_NOT_NULL(world);
    
    Colony colony = create_test_colony();
    uint32_t id = world_add_colony(world, colony);
    
    // Create two disconnected groups with min 5 cells each
    // Group 1: 5 cells in a row
    for (int i = 0; i < 5; i++) {
        world_get_cell(world, i, 0)->colony_id = id;
    }
    // Group 2: 5 cells in a row (separated by gap)
    for (int i = 0; i < 5; i++) {
        world_get_cell(world, 10 + i, 10)->colony_id = id;
    }
    
    Colony* col = world_get_colony(world, id);
    col->cell_count = 10;
    
    size_t initial_colony_count = world->colony_count;
    
    // Check for divisions
    simulation_check_divisions(world);
    
    // Should have created a new colony
    ASSERT_GT(world->colony_count, initial_colony_count);
    
    world_destroy(world);
}

TEST(simulation_division_preserves_cell_count) {
    World* world = world_create(20, 20);
    ASSERT_NOT_NULL(world);
    
    Colony colony = create_test_colony();
    uint32_t id = world_add_colony(world, colony);
    
    // Create two disconnected groups, each >= 5 cells (minimum to avoid tiny fragment cleanup)
    // Group 1: 5 cells at (0,0) to (4,0)
    for (int x = 0; x < 5; x++) {
        world_get_cell(world, x, 0)->colony_id = id;
    }
    // Group 2: 5 cells at (10,10) to (14,10)
    for (int x = 10; x < 15; x++) {
        world_get_cell(world, x, 10)->colony_id = id;
    }
    
    Colony* col = world_get_colony(world, id);
    col->cell_count = 10;
    
    // Count initial cells
    int initial_cells = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id != 0) initial_cells++;
    }
    
    simulation_check_divisions(world);
    
    // Count cells after division
    int final_cells = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id != 0) final_cells++;
    }
    
    // Cell count should be preserved (both groups >= 5, so no cleanup)
    ASSERT_EQ(initial_cells, final_cells);
    
    world_destroy(world);
}

// ============================================================================
// Recombination Tests
// ============================================================================

TEST(simulation_recombination_merges_colonies) {
    World* world = world_create(20, 20);
    ASSERT_NOT_NULL(world);
    
    // Create two identical colonies with parent-child relationship (required for merge)
    Colony colony1 = create_test_colony();
    colony1.genome.spread_rate = 0.5f;
    colony1.genome.mutation_rate = 0.5f;
    colony1.genome.aggression = 0.5f;
    colony1.genome.resilience = 0.5f;
    colony1.genome.metabolism = 0.5f;
    
    Colony colony2 = colony1;  // Identical genome
    
    uint32_t id1 = world_add_colony(world, colony1);
    uint32_t id2 = world_add_colony(world, colony2);
    
    // Set up parent-child relationship
    Colony* col1 = world_get_colony(world, id1);
    Colony* col2 = world_get_colony(world, id2);
    col2->parent_id = id1;
    
    // Place adjacent cells
    world_get_cell(world, 5, 5)->colony_id = id1;
    world_get_cell(world, 6, 5)->colony_id = id2;  // Adjacent (E direction)
    
    col1->cell_count = 1;
    col2->cell_count = 1;
    
    int active_before = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) active_before++;
    }
    
    simulation_check_recombinations(world);
    
    int active_after = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) active_after++;
    }
    
    // One colony should have been deactivated
    ASSERT_LT(active_after, active_before);
    
    world_destroy(world);
}

// ============================================================================
// Empty World Tests
// ============================================================================

TEST(simulation_empty_world_handles_tick_safely) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    // Run many ticks on empty world
    for (int i = 0; i < 100; i++) {
        simulation_tick(world);
    }
    
    ASSERT_EQ(world->tick, 100);
    // Note: spontaneous generation may create colonies in empty worlds
    // Just ensure no crash
    ASSERT_TRUE(1);
    
    world_destroy(world);
}

TEST(simulation_empty_world_spread_is_safe) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    simulation_spread(world);
    
    // Should complete without crash
    ASSERT_TRUE(1);
    
    world_destroy(world);
}

TEST(simulation_empty_world_divisions_is_safe) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    simulation_check_divisions(world);
    
    // Should complete without crash
    ASSERT_TRUE(1);
    
    world_destroy(world);
}

// ============================================================================
// Run Tests
// ============================================================================

int run_world_advanced_tests(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    printf("\n=== Advanced World Tests ===\n\n");
    
    rng_seed(42);
    
    printf("Boundary Tests:\n");
    RUN_TEST(world_get_cell_returns_valid_corner_cells);
    RUN_TEST(world_get_cell_returns_valid_edge_cells);
    RUN_TEST(world_boundary_cells_can_be_assigned);
    
    printf("\nColony Removal Tests:\n");
    RUN_TEST(world_remove_colony_clears_all_cells);
    RUN_TEST(world_remove_colony_preserves_other_colonies);
    
    printf("\nMultiple Colony Tests:\n");
    RUN_TEST(world_init_colonies_do_not_overlap);
    RUN_TEST(world_supports_200_colonies);
    RUN_TEST(world_assigns_unique_colony_ids);
    
    printf("\nWorld Tick Tests:\n");
    RUN_TEST(simulation_tick_increases_cell_age);
    RUN_TEST(simulation_tick_increments_world_counter);
    
    printf("\nSpreading Tests:\n");
    RUN_TEST(simulation_spread_stays_within_bounds);
    RUN_TEST(simulation_spread_expands_from_center);
    
    printf("\nDivision Tests:\n");
    RUN_TEST(simulation_division_creates_new_colony);
    RUN_TEST(simulation_division_preserves_cell_count);
    
    printf("\nRecombination Tests:\n");
    RUN_TEST(simulation_recombination_merges_colonies);
    
    printf("\nEmpty World Tests:\n");
    RUN_TEST(simulation_empty_world_handles_tick_safely);
    RUN_TEST(simulation_empty_world_spread_is_safe);
    RUN_TEST(simulation_empty_world_divisions_is_safe);
    
    printf("\n--- World Advanced Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_world_advanced_tests() > 0 ? 1 : 0;
}
#endif
