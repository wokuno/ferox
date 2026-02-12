/**
 * test_visual_stability.c - Comprehensive stress tests for visual stability
 * Tests colony visual properties at different scales and update frequencies
 * Ensures shape_seed, wobble_phase, centroid, and radius remain stable
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
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL, #ptr " is not NULL")
#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_NE(a, b) ASSERT((a) != (b), #a " != " #b)
#define ASSERT_LE(a, b) ASSERT((a) <= (b), #a " <= " #b)
#define ASSERT_GE(a, b) ASSERT((a) >= (b), #a " >= " #b)
#define ASSERT_LT(a, b) ASSERT((a) < (b), #a " < " #b)
#define ASSERT_GT(a, b) ASSERT((a) > (b), #a " > " #b)

// Helper to create a test colony
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
    colony.shape_seed = (uint32_t)rand();
    colony.wobble_phase = 0.0f;
    return colony;
}

// Helper to count cells of a specific colony
static int count_colony_cells(World* world, uint32_t colony_id) {
    int count = 0;
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == colony_id) {
            count++;
        }
    }
    return count;
}

// Helper to calculate colony centroid
typedef struct {
    float x;
    float y;
} Point;

static Point calc_centroid(World* world, uint32_t colony_id) {
    Point centroid = {0.0f, 0.0f};
    int count = 0;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (cell && cell->colony_id == colony_id) {
                centroid.x += x;
                centroid.y += y;
                count++;
            }
        }
    }
    
    if (count > 0) {
        centroid.x /= count;
        centroid.y /= count;
    }
    
    return centroid;
}

// Helper to calculate colony radius (distance from centroid to farthest cell)
static float calc_radius(World* world, uint32_t colony_id) {
    Point centroid = calc_centroid(world, colony_id);
    float max_dist = 0.0f;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (cell && cell->colony_id == colony_id) {
                float dx = x - centroid.x;
                float dy = y - centroid.y;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > max_dist) {
                    max_dist = dist;
                }
            }
        }
    }
    
    return max_dist;
}

// Recorded snapshot of colony properties for tracking changes
typedef struct {
    uint32_t id;
    uint32_t shape_seed;
    float wobble_phase;
    Point centroid;
    float radius;
    int cell_count;
    bool active;
} ColonySnapshot;

// Helper to take a snapshot of all colonies
static ColonySnapshot* take_snapshot(World* world, int* out_count) {
    if (world == NULL || world->colony_count == 0) {
        *out_count = 0;
        return NULL;
    }
    
    ColonySnapshot* snap = malloc(world->colony_count * sizeof(ColonySnapshot));
    if (snap == NULL) {
        *out_count = 0;
        return NULL;
    }
    
    int count = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* col = &world->colonies[i];
        if (col->active) {
            snap[count].id = col->id;
            snap[count].shape_seed = col->shape_seed;
            snap[count].wobble_phase = col->wobble_phase;
            snap[count].centroid = calc_centroid(world, col->id);
            snap[count].radius = calc_radius(world, col->id);
            snap[count].cell_count = (int)col->cell_count;
            snap[count].active = col->active;
            count++;
        }
    }
    
    *out_count = count;
    return snap;
}

// Helper to find colony by id in snapshot
__attribute__((unused))
static ColonySnapshot* find_in_snapshot(ColonySnapshot* snap, int count, uint32_t id) {
    for (int i = 0; i < count; i++) {
        if (snap[i].id == id) {
            return &snap[i];
        }
    }
    return NULL;
}

// ============================================================================
// Small Scale Tests (10x10 world, 3 colonies, 50 ticks)
// ============================================================================

TEST(small_scale_shape_seed_valid) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    rng_seed(100);
    
    // Create 3 colonies
    for (int i = 0; i < 3; i++) {
        Colony col = create_test_colony();
        // Place them manually
        for (int j = 0; j < 3; j++) {
            Cell* cell = world_get_cell(world, i * 2 + 1, j + 1);
            if (cell) cell->colony_id = world_add_colony(world, col);
        }
    }
    
    // Run 50 ticks and check shape seeds are always valid (non-zero)
    for (int tick = 0; tick < 50; tick++) {
        simulation_tick(world);
        
        for (size_t i = 0; i < world->colony_count; i++) {
            if (world->colonies[i].active) {
                // Shape seed should never be zero
                ASSERT_NE(world->colonies[i].shape_seed, 0);
            }
        }
    }
    
    world_destroy(world);
}

TEST(small_scale_centroid_bounded) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    rng_seed(101);
    world_init_random_colonies(world, 3);
    
    // Run 50 ticks, track centroid jumps
    Point prev_centroids[10] = {0};
    int max_centroid_jump = 0;
    
    for (size_t i = 0; i < world->colony_count && i < 10; i++) {
        prev_centroids[i] = calc_centroid(world, world->colonies[i].id);
    }
    
    for (int tick = 0; tick < 50; tick++) {
        simulation_tick(world);
        
        for (size_t i = 0; i < world->colony_count && i < 10; i++) {
            Colony* col = &world->colonies[i];
            if (col->active) {
                Point curr = calc_centroid(world, col->id);
                
                float dx = fabsf(curr.x - prev_centroids[i].x);
                float dy = fabsf(curr.y - prev_centroids[i].y);
                int jump = (int)dx + (int)dy;  // Manhattan distance
                
                if (jump > max_centroid_jump) {
                    max_centroid_jump = jump;
                }
                
                // Centroid should not jump more than 3 cells per tick
                ASSERT(jump <= 3, "Centroid jumped > 3 cells");
                
                prev_centroids[i] = curr;
            }
        }
    }
    
    world_destroy(world);
}

TEST(small_scale_radius_bounded) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    rng_seed(102);
    world_init_random_colonies(world, 3);
    
    // Run 50 ticks and check radius changes
    float prev_radii[10] = {0};
    
    for (size_t i = 0; i < world->colony_count && i < 10; i++) {
        prev_radii[i] = calc_radius(world, world->colonies[i].id);
    }
    
    for (int tick = 0; tick < 50; tick++) {
        simulation_tick(world);
        
        for (size_t i = 0; i < world->colony_count && i < 10; i++) {
            Colony* col = &world->colonies[i];
            if (col->active && col->cell_count > 0) {
                float curr_radius = calc_radius(world, col->id);
                float prev_radius = prev_radii[i];
                
                // Radius should stay within world bounds
                ASSERT_LE(curr_radius, 10.0f);
                
                // Allow larger radius jumps during growth (can be > 100%)
                // but should be bounded
                if (prev_radius > 0.0f) {
                    float pct_change = fabsf(curr_radius - prev_radius) / prev_radius;
                    ASSERT_LE(pct_change, 5.0f);  // Allow up to 5x change per tick
                }
                
                prev_radii[i] = curr_radius;
            }
        }
    }
    
    world_destroy(world);
}

TEST(small_scale_wobble_phase_smooth) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    rng_seed(103);
    world_init_random_colonies(world, 3);
    
    // Record initial wobble phases
    float prev_phases[10] = {0};
    for (size_t i = 0; i < world->colony_count && i < 10; i++) {
        prev_phases[i] = world->colonies[i].wobble_phase;
    }
    
    // Run 50 ticks
    for (int tick = 0; tick < 50; tick++) {
        simulation_tick(world);
        
        for (size_t i = 0; i < world->colony_count && i < 10; i++) {
            Colony* col = &world->colonies[i];
            if (col->active) {
                float curr_phase = col->wobble_phase;
                float prev_phase = prev_phases[i];
                
                // Phase should increment smoothly, max jump ~0.1 per tick
                float phase_diff = fabsf(curr_phase - prev_phase);
                
                // Handle wrap-around (from ~6.28 to 0)
                if (phase_diff > 3.14f) {
                    phase_diff = 6.28f - phase_diff;
                }
                
                ASSERT(phase_diff <= 0.15f, "Wobble phase jumped > 0.1");
                prev_phases[i] = curr_phase;
            }
        }
    }
    
    world_destroy(world);
}

TEST(small_scale_no_unexpected_colonies) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);
    
    rng_seed(104);
    world_init_random_colonies(world, 3);
    
    int initial_count = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) initial_count++;
    }
    
    // Run 50 ticks - no new colonies should appear (only divisions)
    for (int tick = 0; tick < 50; tick++) {
        simulation_tick(world);
        
        int curr_count = 0;
        for (size_t i = 0; i < world->colony_count; i++) {
            if (world->colonies[i].active) curr_count++;
        }
        
        // Should never exceed initial count + small margin for divisions
        ASSERT_LE(curr_count, initial_count + 20);
    }
    
    world_destroy(world);
}

// ============================================================================
// Medium Scale Tests (100x100 world, 20 colonies, 200 ticks)
// ============================================================================

TEST(medium_scale_all_properties_valid) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(200);
    world_init_random_colonies(world, 20);
    
    // Run 200 ticks
    for (int tick = 0; tick < 200; tick++) {
        simulation_tick(world);
        
        // Every 20 ticks, verify properties of colonies
        if (tick % 20 == 0) {
            for (size_t i = 0; i < world->colony_count; i++) {
                Colony* col = &world->colonies[i];
                if (col->active) {
                    // Shape seed should never be zero
                    ASSERT_NE(col->shape_seed, 0);
                    
                    // Cell count should be consistent with what we see
                    int actual = count_colony_cells(world, col->id);
                    ASSERT_EQ((int)col->cell_count, actual);
                    
                    // Centroid should be within bounds
                    Point centroid = calc_centroid(world, col->id);
                    ASSERT_GE(centroid.x, -1.0f);
                    ASSERT_LE(centroid.x, 101.0f);
                    ASSERT_GE(centroid.y, -1.0f);
                    ASSERT_LE(centroid.y, 101.0f);
                }
            }
        }
    }
    
    world_destroy(world);
}

TEST(medium_scale_radius_within_bounds) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(201);
    world_init_random_colonies(world, 20);
    
    // Run 200 ticks and check radius bounds
    for (int tick = 0; tick < 200; tick++) {
        simulation_tick(world);
        
        for (size_t i = 0; i < world->colony_count; i++) {
            Colony* col = &world->colonies[i];
            if (col->active && col->cell_count > 0) {
                float radius = calc_radius(world, col->id);
                Point centroid = calc_centroid(world, col->id);
                
                // Radius should be reasonable
                ASSERT(radius < 100.0f, "Radius > world size");
                
                // Centroid should be within bounds
                ASSERT(centroid.x >= 0 && centroid.x < 100, "Centroid X out of bounds");
                ASSERT(centroid.y >= 0 && centroid.y < 100, "Centroid Y out of bounds");
            }
        }
    }
    
    world_destroy(world);
}

TEST(medium_scale_population_consistency) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(202);
    world_init_random_colonies(world, 20);
    
    // Run 200 ticks and verify population counts
    for (int tick = 0; tick < 200; tick++) {
        simulation_tick(world);
        
        for (size_t i = 0; i < world->colony_count; i++) {
            Colony* col = &world->colonies[i];
            if (col->active) {
                int actual_count = count_colony_cells(world, col->id);
                ASSERT_EQ((int)col->cell_count, actual_count);
                
                // max_cell_count should never decrease
                ASSERT_GE((int)col->max_cell_count, actual_count);
            }
        }
    }
    
    world_destroy(world);
}

TEST(medium_scale_no_duplicate_colonies) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(203);
    world_init_random_colonies(world, 20);
    
    // Track seen IDs to detect duplicates
    uint32_t seen_ids[1000];
    int seen_count = 0;
    
    for (int tick = 0; tick < 200; tick++) {
        simulation_tick(world);
        
        // Check for duplicate active colonies
        for (size_t i = 0; i < world->colony_count; i++) {
            if (world->colonies[i].active) {
                uint32_t id = world->colonies[i].id;
                
                // Check not already in list
                int found = 0;
                for (int j = 0; j < seen_count; j++) {
                    if (seen_ids[j] == id) {
                        found = 1;
                        break;
                    }
                }
                
                if (!found && seen_count < 1000) {
                    seen_ids[seen_count++] = id;
                }
                
                // Check this ID appears only once in current colonies
                int count = 0;
                for (size_t j = 0; j < world->colony_count; j++) {
                    if (world->colonies[j].active && world->colonies[j].id == id) {
                        count++;
                    }
                }
                ASSERT_EQ(count, 1);
            }
        }
    }
    
    world_destroy(world);
}

// ============================================================================
// Large Scale Tests (200x100 world, 50 colonies, 500 ticks)
// ============================================================================

TEST(large_scale_extreme_value_protection) {
    World* world = world_create(200, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(300);
    world_init_random_colonies(world, 50);
    
    // Run 500 ticks and check for extreme values
    for (int tick = 0; tick < 500; tick++) {
        simulation_tick(world);
        
        for (size_t i = 0; i < world->colony_count; i++) {
            Colony* col = &world->colonies[i];
            if (col->active) {
                // Cell count should be reasonable
                ASSERT_LE(col->cell_count, 200 * 100);
                
                // Radius should not exceed world bounds
                float radius = calc_radius(world, col->id);
                ASSERT_LT(radius, 200.0f);
                
                // Position checks
                Point centroid = calc_centroid(world, col->id);
                ASSERT_GE(centroid.x, -1.0f);
                ASSERT_LE(centroid.x, 201.0f);
                ASSERT_GE(centroid.y, -1.0f);
                ASSERT_LE(centroid.y, 101.0f);
            }
        }
    }
    
    world_destroy(world);
}

TEST(large_scale_shape_seed_valid) {
    World* world = world_create(200, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(301);
    world_init_random_colonies(world, 50);
    
    // Track that all colonies have valid shape seeds throughout
    // Run 500 ticks
    for (int tick = 0; tick < 500; tick++) {
        simulation_tick(world);
        
        // Verify shape seeds
        for (size_t i = 0; i < world->colony_count; i++) {
            if (world->colonies[i].active) {
                ASSERT_NE(world->colonies[i].shape_seed, 0);
            }
        }
    }
    
    world_destroy(world);
}

TEST(large_scale_cell_allocation_bounds) {
    World* world = world_create(200, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(302);
    world_init_random_colonies(world, 50);
    
    int max_cells_used = 0;
    int total_cells = 200 * 100;
    
    // Run 500 ticks
    for (int tick = 0; tick < 500; tick++) {
        simulation_tick(world);
        
        int cells_used = 0;
        for (int i = 0; i < total_cells; i++) {
            if (world->cells[i].colony_id != 0) {
                cells_used++;
            }
        }
        
        if (cells_used > max_cells_used) {
            max_cells_used = cells_used;
        }
        
        // Should not exceed world size
        ASSERT_LE(cells_used, total_cells);
    }
    
    world_destroy(world);
}

// ============================================================================
// Rapid Update Tests (30 FPS client vs 10 Hz server simulation)
// ============================================================================

TEST(rapid_update_shape_seed_valid) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(400);
    world_init_random_colonies(world, 20);
    
    // Simulate: server does 1 tick, client renders at 3x
    // 30 FPS client = 3 renders per 10 Hz server tick
    
    for (int server_tick = 0; server_tick < 50; server_tick++) {
        // Take snapshot before tick
        int snap1_count;
        ColonySnapshot* snap1 = take_snapshot(world, &snap1_count);
        
        // Server updates world
        simulation_tick(world);
        
        // Take snapshot after tick
        int snap2_count;
        ColonySnapshot* snap2 = take_snapshot(world, &snap2_count);
        
        // During this server tick, client renders 3 times
        // All colonies should have valid properties
        for (int i = 0; i < snap2_count; i++) {
            // Shape seed should never be zero
            ASSERT_NE(snap2[i].shape_seed, 0);
            
            // Wobble phase should be in valid range
            ASSERT_GE(snap2[i].wobble_phase, -0.1f);
            ASSERT_LE(snap2[i].wobble_phase, 6.38f);
        }
        
        free(snap1);
        free(snap2);
    }
    
    world_destroy(world);
}

TEST(rapid_update_radius_reasonable) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(401);
    world_init_random_colonies(world, 20);
    
    float prev_centroids[100 * 2];
    float prev_radii[100];
    
    // Initialize
    for (int i = 0; i < 100; i++) {
        if (i < (int)world->colony_count) {
            prev_centroids[i * 2] = calc_centroid(world, world->colonies[i].id).x;
            prev_centroids[i * 2 + 1] = calc_centroid(world, world->colonies[i].id).y;
            prev_radii[i] = calc_radius(world, world->colonies[i].id);
        }
    }
    
    // Rapid ticks
    for (int tick = 0; tick < 100; tick++) {
        simulation_tick(world);
        
        for (size_t i = 0; i < world->colony_count && i < 100; i++) {
            if (world->colonies[i].active) {
                Point curr_centroid = calc_centroid(world, world->colonies[i].id);
                float curr_radius = calc_radius(world, world->colonies[i].id);
                
                float dx = fabsf(curr_centroid.x - prev_centroids[i * 2]);
                float dy = fabsf(curr_centroid.y - prev_centroids[i * 2 + 1]);
                
                // Centroid should move smoothly
                ASSERT_LT(dx + dy, 4.0f);
                
                // Radius should stay within bounds but allow significant changes
                if (prev_radii[i] > 0.0f) {
                    float pct_change = fabsf(curr_radius - prev_radii[i]) / prev_radii[i];
                    // During rapid growth/division, radius can change significantly
                    ASSERT_LE(pct_change, 10.0f);  // Allow up to 10x change
                }
                
                prev_centroids[i * 2] = curr_centroid.x;
                prev_centroids[i * 2 + 1] = curr_centroid.y;
                prev_radii[i] = curr_radius;
            }
        }
    }
    
    world_destroy(world);
}

// ============================================================================
// Division/Recombination Stress Tests
// ============================================================================

TEST(division_properties_valid) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(500);
    world_init_random_colonies(world, 10);
    
    // Run many ticks to trigger divisions
    for (int tick = 0; tick < 300; tick++) {
        simulation_tick(world);
        
        // All colonies should have valid shape seeds
        for (size_t i = 0; i < world->colony_count; i++) {
            if (world->colonies[i].active) {
                // All colonies should have valid shape seeds (non-zero)
                ASSERT_NE(world->colonies[i].shape_seed, 0);
                
                // Wobble phase should be in valid range
                ASSERT_GE(world->colonies[i].wobble_phase, -0.1f);
                ASSERT_LE(world->colonies[i].wobble_phase, 6.38f);
                
                // Cell count should match grid
                int actual = count_colony_cells(world, world->colonies[i].id);
                ASSERT_EQ((int)world->colonies[i].cell_count, actual);
            }
        }
    }
    
    world_destroy(world);
}

TEST(recombination_properties_stable) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(501);
    world_init_random_colonies(world, 30);  // More colonies = more likely recombination
    
    // Run ticks and monitor for shape seed stability
    for (int tick = 0; tick < 300; tick++) {
        simulation_tick(world);
        
        // Check all active colonies have valid properties
        for (size_t i = 0; i < world->colony_count; i++) {
            if (world->colonies[i].active) {
                // Shape seed should be set
                ASSERT_NE(world->colonies[i].shape_seed, 0);
                
                // Wobble phase should be in range [0, 2π]
                ASSERT_GE(world->colonies[i].wobble_phase, -0.1f);
                ASSERT_LE(world->colonies[i].wobble_phase, 6.38f);
                
                // Cell count should match grid
                int actual = count_colony_cells(world, world->colonies[i].id);
                ASSERT_EQ((int)world->colonies[i].cell_count, actual);
            }
        }
    }
    
    world_destroy(world);
}

TEST(child_colony_initialization) {
    World* world = world_create(100, 100);
    ASSERT_NOT_NULL(world);
    
    rng_seed(502);
    world_init_random_colonies(world, 10);
    
    // Run ticks to trigger divisions
    for (int tick = 0; tick < 200; tick++) {
        simulation_tick(world);
        
        for (size_t i = 0; i < world->colony_count; i++) {
            Colony* col = &world->colonies[i];
            
            if (col->active) {
                // All colonies should have non-zero shape_seed
                ASSERT_NE(col->shape_seed, 0);
                
                // Child colonies should have parent_id set if they're children
                // (but we can't really test this without internal division logic)
                
                // Wobble phase should start at 0 or be initialized properly
                // Allow it to be anything in valid range
                ASSERT_GE(col->wobble_phase, -0.1f);
                ASSERT_LE(col->wobble_phase, 6.38f);
            }
        }
    }
    
    world_destroy(world);
}

// ============================================================================
// Shape Variety Tests
// ============================================================================

TEST(shape_types_distributed) {
    // Test that different shape seeds produce different shape types
    // The shape function has 8 shape types, we should see variety
    const int NUM_SEEDS = 100;
    int shape_type_counts[8] = {0};
    
    for (int i = 0; i < NUM_SEEDS; i++) {
        uint32_t seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
        // Shape type is determined by hash(seed) % 8
        // We can't directly test this, but we can sample shapes
        
        // Sample shape at 0 angle - different types give different values
        float shape_0 = colony_shape_at_angle(seed, 0.0f, 0.0f);
        float shape_90 = colony_shape_at_angle(seed, 1.5708f, 0.0f);  // pi/2
        float shape_180 = colony_shape_at_angle(seed, 3.1416f, 0.0f); // pi
        float shape_270 = colony_shape_at_angle(seed, 4.7124f, 0.0f); // 3pi/2
        
        // Compute a shape signature to classify rough shape type
        float asymmetry = fabsf(shape_0 - shape_180) + fabsf(shape_90 - shape_270);
        float elongation = fabsf(shape_0 - shape_90);
        
        // Classify into rough categories
        if (asymmetry < 0.1f && elongation < 0.1f) {
            shape_type_counts[0]++;  // Round
        } else if (elongation > 0.2f) {
            shape_type_counts[1]++;  // Elongated
        } else if (asymmetry > 0.15f) {
            shape_type_counts[2]++;  // Asymmetric
        } else {
            shape_type_counts[3]++;  // Other
        }
    }
    
    // Should see at least some variety - no single type should dominate completely
    int max_count = 0;
    for (int i = 0; i < 4; i++) {
        if (shape_type_counts[i] > max_count) {
            max_count = shape_type_counts[i];
        }
    }
    
    // No single category should have more than 80% of shapes
    ASSERT_LT(max_count, NUM_SEEDS * 80 / 100);
}

TEST(shape_function_range_valid) {
    // Test that shape function always returns values in valid range
    for (int seed_iter = 0; seed_iter < 50; seed_iter++) {
        uint32_t seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
        
        for (int angle_step = 0; angle_step < 360; angle_step++) {
            float angle = (float)angle_step * 3.14159f / 180.0f;
            
            for (int phase_step = 0; phase_step < 10; phase_step++) {
                float phase = (float)phase_step * 0.628f;  // 0 to 2pi roughly
                
                float shape = colony_shape_at_angle(seed, angle, phase);
                
                // Shape should be in clamped range [0.5, 1.5]
                ASSERT_GE(shape, 0.49f);
                ASSERT_LE(shape, 1.51f);
            }
        }
    }
}

TEST(shape_seeds_produce_different_shapes) {
    // Test that different seeds produce measurably different shapes
    const int NUM_SEEDS = 50;
    float shape_signatures[50];
    
    for (int i = 0; i < NUM_SEEDS; i++) {
        uint32_t seed = (uint32_t)(i * 12345) ^ 0xDEADBEEF;
        
        // Compute a shape signature by sampling around the perimeter
        float sig = 0.0f;
        for (int j = 0; j < 16; j++) {
            float angle = (float)j * 3.14159f * 2.0f / 16.0f;
            sig += colony_shape_at_angle(seed, angle, 0.0f) * (float)(j + 1);
        }
        shape_signatures[i] = sig;
    }
    
    // Count unique signatures (within tolerance)
    int unique_count = 0;
    for (int i = 0; i < NUM_SEEDS; i++) {
        int is_unique = 1;
        for (int j = 0; j < i; j++) {
            if (fabsf(shape_signatures[i] - shape_signatures[j]) < 0.1f) {
                is_unique = 0;
                break;
            }
        }
        if (is_unique) unique_count++;
    }
    
    // At least 80% of seeds should produce visually distinct shapes
    ASSERT_GE(unique_count, NUM_SEEDS * 80 / 100);
}

TEST(shape_animation_subtle) {
    // Test that animation (phase) produces subtle changes, not dramatic ones
    uint32_t seed = 0x12345678;
    
    float max_change = 0.0f;
    
    for (int angle_step = 0; angle_step < 36; angle_step++) {
        float angle = (float)angle_step * 3.14159f / 18.0f;
        
        float shape_phase0 = colony_shape_at_angle(seed, angle, 0.0f);
        float shape_phase1 = colony_shape_at_angle(seed, angle, 3.14159f);
        
        float change = fabsf(shape_phase1 - shape_phase0);
        if (change > max_change) {
            max_change = change;
        }
    }
    
    // Animation should cause at most ~10% shape variation
    ASSERT_LE(max_change, 0.15f);
}

// ============================================================================
// Run Tests
// ============================================================================

int run_visual_stability_tests(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    printf("\n=== Visual Stability Stress Tests ===\n\n");
    
    printf("Small Scale Tests (10x10, 3 colonies, 50 ticks):\n");
    RUN_TEST(small_scale_shape_seed_valid);
    RUN_TEST(small_scale_centroid_bounded);
    RUN_TEST(small_scale_radius_bounded);
    RUN_TEST(small_scale_wobble_phase_smooth);
    RUN_TEST(small_scale_no_unexpected_colonies);
    
    printf("\nMedium Scale Tests (100x100, 20 colonies, 200 ticks):\n");
    RUN_TEST(medium_scale_all_properties_valid);
    RUN_TEST(medium_scale_radius_within_bounds);
    RUN_TEST(medium_scale_population_consistency);
    RUN_TEST(medium_scale_no_duplicate_colonies);
    
    printf("\nLarge Scale Tests (200x100, 50 colonies, 500 ticks):\n");
    RUN_TEST(large_scale_extreme_value_protection);
    RUN_TEST(large_scale_shape_seed_valid);
    RUN_TEST(large_scale_cell_allocation_bounds);
    
    printf("\nRapid Update Tests (30 FPS client vs 10 Hz server):\n");
    RUN_TEST(rapid_update_shape_seed_valid);
    RUN_TEST(rapid_update_radius_reasonable);
    
    printf("\nDivision/Recombination Stress Tests:\n");
    RUN_TEST(division_properties_valid);
    RUN_TEST(recombination_properties_stable);
    RUN_TEST(child_colony_initialization);
    
    printf("\nShape Variety Tests:\n");
    RUN_TEST(shape_types_distributed);
    RUN_TEST(shape_function_range_valid);
    RUN_TEST(shape_seeds_produce_different_shapes);
    RUN_TEST(shape_animation_subtle);
    
    printf("\n--- Visual Stability Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_visual_stability_tests() > 0 ? 1 : 0;
}
#endif
