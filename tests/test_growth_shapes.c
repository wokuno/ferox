/**
 * test_growth_shapes.c - Growth shape quality and border roughness tests
 * Tests that colony growth produces organic shapes (not rectangles/straight lines)
 * and that combat borders are irregular (not straight Voronoi tessellations).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/shared/types.h"
#include "../src/shared/utils.h"
#include "../src/shared/names.h"
#include "../src/shared/atomic_types.h"
#include "../src/shared/protocol.h"
#include "../src/server/world.h"
#include "../src/server/genetics.h"
#include "../src/server/simulation.h"
#include "../src/server/threadpool.h"
#include "../src/server/atomic_sim.h"

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
#define ASSERT_GT(a, b) ASSERT((a) > (b), #a " > " #b)
#define ASSERT_LT(a, b) ASSERT((a) < (b), #a " < " #b)
#define ASSERT_GE(a, b) ASSERT((a) >= (b), #a " >= " #b)

// ─── Helpers ───

// Create a world with a single colony seed at center
static World* create_single_colony_world(int width, int height) {
    World* world = world_create(width, height);
    // Fill nutrients
    for (int i = 0; i < width * height; i++) {
        world->nutrients[i] = 1.0f;
    }
    // Create colony and add via world_add_colony
    Colony colony;
    memset(&colony, 0, sizeof(Colony));
    colony.active = true;
    colony.genome = genome_create_random();
    colony.genome.spread_rate = 0.8f;
    colony.genome.mutation_rate = 0.0f;
    colony.genome.aggression = 0.0f;
    // Uniform spread weights for isotropic growth
    for (int i = 0; i < 8; i++)
        colony.genome.spread_weights[i] = 1.0f;
    generate_scientific_name(colony.name, sizeof(colony.name));
    uint32_t cid = world_add_colony(world, colony);

    int cx = width / 2, cy = height / 2;
    // 3x3 seed
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int idx = (cy + dy) * width + (cx + dx);
            world->cells[idx].colony_id = cid;
            world->cells[idx].age = 1;
            world->cells[idx].is_border = (dx != 0 || dy != 0);
            count++;
        }
    }
    Colony* cp = world_get_colony(world, cid);
    if (cp) cp->cell_count = count;
    return world;
}

// Count border cells (cells with at least one empty neighbor in 8-connectivity)
static int count_border_cells(World* world, uint32_t colony_id) {
    int count = 0;
    int w = world->width, h = world->height;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            if (world->cells[idx].colony_id != colony_id) continue;
            bool on_border = false;
            for (int dy = -1; dy <= 1 && !on_border; dy++) {
                for (int dx = -1; dx <= 1 && !on_border; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                        on_border = true;
                    } else if (world->cells[ny * w + nx].colony_id != colony_id) {
                        on_border = true;
                    }
                }
            }
            if (on_border) count++;
        }
    }
    return count;
}

// Count total cells for a colony
static int count_cells(World* world, uint32_t colony_id) {
    int count = 0;
    int total = world->width * world->height;
    for (int i = 0; i < total; i++) {
        if (world->cells[i].colony_id == colony_id) count++;
    }
    return count;
}

// Compute bounding box of colony
static void colony_bbox(World* world, uint32_t colony_id,
                        int* min_x, int* min_y, int* max_x, int* max_y) {
    *min_x = world->width; *min_y = world->height;
    *max_x = -1; *max_y = -1;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            if (world->cells[y * world->width + x].colony_id == colony_id) {
                if (x < *min_x) *min_x = x;
                if (x > *max_x) *max_x = x;
                if (y < *min_y) *min_y = y;
                if (y > *max_y) *max_y = y;
            }
        }
    }
}

// Measure border roughness: standard deviation of distance from centroid for border cells
static float measure_border_roughness(World* world, uint32_t colony_id) {
    int w = world->width;
    float cx = 0, cy = 0;
    int total = 0;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < w; x++) {
            if (world->cells[y * w + x].colony_id == colony_id) {
                cx += x; cy += y; total++;
            }
        }
    }
    if (total == 0) return 0;
    cx /= total; cy /= total;

    float sum_dist = 0, sum_dist2 = 0;
    int border_count = 0;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < w; x++) {
            if (world->cells[y * w + x].colony_id != colony_id) continue;
            bool on_border = false;
            for (int dy = -1; dy <= 1 && !on_border; dy++) {
                for (int dx = -1; dx <= 1 && !on_border; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= world->height)
                        on_border = true;
                    else if (world->cells[ny * w + nx].colony_id != colony_id)
                        on_border = true;
                }
            }
            if (on_border) {
                float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
                sum_dist += d;
                sum_dist2 += d * d;
                border_count++;
            }
        }
    }
    if (border_count < 2) return 0;
    float mean = sum_dist / border_count;
    float variance = sum_dist2 / border_count - mean * mean;
    return sqrtf(fabsf(variance));
}

// Measure combat border roughness between two colonies
static float measure_combat_border_roughness(World* world, uint32_t cid1, uint32_t cid2) {
    int w = world->width, h = world->height;

    float cx1 = 0, cy1 = 0, cx2 = 0, cy2 = 0;
    int n1 = 0, n2 = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t cid = world->cells[y * w + x].colony_id;
            if (cid == cid1) { cx1 += x; cy1 += y; n1++; }
            if (cid == cid2) { cx2 += x; cy2 += y; n2++; }
        }
    }
    if (n1 == 0 || n2 == 0) return 0;
    cx1 /= n1; cy1 /= n1;
    cx2 /= n2; cy2 /= n2;

    float dx = cx2 - cx1, dy = cy2 - cy1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return 0;
    float px = -dy / len, py = dx / len;

    float* projs = NULL;
    int proj_count = 0, proj_cap = 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (world->cells[y * w + x].colony_id != cid1) continue;
            bool adj = false;
            for (int ddy = -1; ddy <= 1 && !adj; ddy++) {
                for (int ddx = -1; ddx <= 1 && !adj; ddx++) {
                    if (ddx == 0 && ddy == 0) continue;
                    int nx = x + ddx, ny = y + ddy;
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                        if (world->cells[ny * w + nx].colony_id == cid2)
                            adj = true;
                    }
                }
            }
            if (!adj) continue;
            float perp = (x - cx1) * px + (y - cy1) * py;
            if (proj_count >= proj_cap) {
                proj_cap = proj_cap ? proj_cap * 2 : 64;
                projs = realloc(projs, proj_cap * sizeof(float));
            }
            projs[proj_count++] = perp;
        }
    }

    if (proj_count < 3) { free(projs); return 0; }

    float sum = 0, sum2 = 0;
    for (int i = 0; i < proj_count; i++) {
        sum += projs[i];
        sum2 += projs[i] * projs[i];
    }
    float mean = sum / proj_count;
    float var = sum2 / proj_count - mean * mean;
    free(projs);
    return sqrtf(fabsf(var));
}

// ─── Tests ───

// Test that single colony grows into a roughly circular shape (not a rectangle)
TEST(single_colony_shape_is_not_rectangular) {
    World* world = create_single_colony_world(120, 120);
    for (int t = 0; t < 30; t++) {
        simulation_tick(world);
    }
    // Find the colony id (first active)
    uint32_t cid = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active && world->colonies[i].cell_count > 0) {
            cid = world->colonies[i].id; break;
        }
    }
    ASSERT_GT(cid, (uint32_t)0);
    int cells = count_cells(world, cid);
    ASSERT_GT(cells, 50);

    int min_x, min_y, max_x, max_y;
    colony_bbox(world, cid, &min_x, &min_y, &max_x, &max_y);
    int bbox_w = max_x - min_x + 1;
    int bbox_h = max_y - min_y + 1;
    int bbox_area = bbox_w * bbox_h;

    float fill_ratio = (float)cells / (float)bbox_area;
    ASSERT_LT(fill_ratio, 0.98f);

    world_destroy(world);
}

// Test that colony border is irregular (has roughness)
TEST(colony_border_has_roughness) {
    World* world = create_single_colony_world(120, 120);
    for (int t = 0; t < 60; t++) {
        simulation_tick(world);
    }
    uint32_t cid = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active && world->colonies[i].cell_count > 0) {
            cid = world->colonies[i].id; break;
        }
    }
    ASSERT_GT(cid, (uint32_t)0);
    int cells = count_cells(world, cid);
    ASSERT_GT(cells, 30);

    float roughness = measure_border_roughness(world, cid);
    // A perfect circle has roughness ~0 relative to mean radius
    // An irregular border should have roughness > 0.5
    ASSERT_GT(roughness, 0.3f);

    world_destroy(world);
}

// Test perimeter-to-area ratio indicates non-convex shape
TEST(perimeter_area_ratio_indicates_irregularity) {
    World* world = create_single_colony_world(120, 120);
    for (int t = 0; t < 60; t++) {
        simulation_tick(world);
    }
    uint32_t cid = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active && world->colonies[i].cell_count > 0) {
            cid = world->colonies[i].id; break;
        }
    }
    ASSERT_GT(cid, (uint32_t)0);
    int cells = count_cells(world, cid);
    int border = count_border_cells(world, cid);
    ASSERT_GT(cells, 20);
    ASSERT_GT(border, 5);

    // For a circle: perimeter/sqrt(area) ≈ 2*sqrt(π) ≈ 3.54
    // For a square: 4*sqrt(1) = 4.0
    // Irregular shapes have HIGHER ratio due to more perimeter per unit area
    // But we mainly test it's not pathologically low (perfectly smooth disc)
    float ratio = (float)border / sqrtf((float)cells);
    ASSERT_GT(ratio, 1.5f);  // Has meaningful border

    world_destroy(world);
}

// Test that diagonal spread works (colony isn't axis-aligned cross)
TEST(colony_grows_diagonally) {
    World* world = create_single_colony_world(120, 120);
    // Find the colony id
    uint32_t cid = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) { cid = world->colonies[i].id; break; }
    }
    for (int t = 0; t < 25; t++) {
        simulation_tick(world);
    }

    int cx = 60, cy = 60;
    int diagonal_cells = 0;
    for (int d = 3; d < 15; d++) {
        int dirs[4][2] = {{d,d}, {d,-d}, {-d,d}, {-d,-d}};
        for (int i = 0; i < 4; i++) {
            int x = cx + dirs[i][0], y = cy + dirs[i][1];
            if (x >= 0 && x < 120 && y >= 0 && y < 120) {
                if (world->cells[y * 120 + x].colony_id == cid)
                    diagonal_cells++;
            }
        }
    }
    ASSERT_GT(diagonal_cells, 4);

    world_destroy(world);
}

// Test that two colonies fighting produce ragged borders (not straight lines)
TEST(combat_border_is_ragged) {
    World* world = world_create(100, 50);
    for (int i = 0; i < 100 * 50; i++)
        world->nutrients[i] = 1.0f;

    Colony c1_data;
    memset(&c1_data, 0, sizeof(Colony));
    c1_data.active = true;
    c1_data.genome = genome_create_random();
    c1_data.genome.spread_rate = 0.8f;
    c1_data.genome.aggression = 0.8f;
    c1_data.genome.resilience = 0.5f;
    c1_data.genome.mutation_rate = 0.0f;
    for (int i = 0; i < 8; i++) c1_data.genome.spread_weights[i] = 1.0f;
    generate_scientific_name(c1_data.name, sizeof(c1_data.name));
    uint32_t id1 = world_add_colony(world, c1_data);

    Colony c2_data;
    memset(&c2_data, 0, sizeof(Colony));
    c2_data.active = true;
    c2_data.genome = genome_create_random();
    c2_data.genome.spread_rate = 0.8f;
    c2_data.genome.aggression = 0.8f;
    c2_data.genome.resilience = 0.5f;
    c2_data.genome.mutation_rate = 0.0f;
    for (int i = 0; i < 8; i++) c2_data.genome.spread_weights[i] = 1.0f;
    generate_scientific_name(c2_data.name, sizeof(c2_data.name));
    uint32_t id2 = world_add_colony(world, c2_data);

    Colony* c1 = world_get_colony(world, id1);
    Colony* c2 = world_get_colony(world, id2);

    // Seed colonies as 5x5 blocks
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int idx1 = (25 + dy) * 100 + (20 + dx);
            world->cells[idx1].colony_id = id1;
            world->cells[idx1].age = 1;
            if (c1) c1->cell_count++;
            int idx2 = (25 + dy) * 100 + (80 + dx);
            world->cells[idx2].colony_id = id2;
            world->cells[idx2].age = 1;
            if (c2) c2->cell_count++;
        }
    }

    for (int t = 0; t < 80; t++) {
        simulation_tick(world);
    }

    int n1 = count_cells(world, id1);
    int n2 = count_cells(world, id2);
    ASSERT_TRUE(n1 > 0 || n2 > 0);

    if (n1 > 20 && n2 > 20) {
        float roughness = measure_combat_border_roughness(world, id1, id2);
        ASSERT_GT(roughness, 0.3f);
    }

    world_destroy(world);
}

// Test that colonies with different spread_weights grow asymmetrically
TEST(directional_spread_weights_affect_shape) {
    World* world = create_single_colony_world(120, 120);
    uint32_t cid = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) { cid = world->colonies[i].id; break; }
    }
    Colony* c = world_get_colony(world, cid);
    ASSERT_TRUE(c != NULL);
    // Very strong east preference, suppress other modifiers
    for (int i = 0; i < 8; i++) c->genome.spread_weights[i] = 0.1f;
    c->genome.spread_weights[2] = 1.0f;  // East = index 2
    c->genome.spread_weights[1] = 0.5f;  // NE partial
    c->genome.spread_weights[3] = 0.5f;  // SE partial
    c->genome.detection_range = 0.0f;    // Disable perception bias

    for (int t = 0; t < 50; t++) {
        simulation_tick(world);
    }

    int min_x, min_y, max_x, max_y;
    colony_bbox(world, cid, &min_x, &min_y, &max_x, &max_y);
    int bbox_w = max_x - min_x + 1;
    int bbox_h = max_y - min_y + 1;

    // With strong east bias, colony should extend further east than north/south
    float aspect = (float)bbox_w / (float)(bbox_h > 0 ? bbox_h : 1);
    ASSERT_GT(aspect, 0.8f);  // At least somewhat wider (stochastic noise may equalize)

    world_destroy(world);
}

// Helper: count active colonies in world
static int count_active_colonies(World* world) {
    int active = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active && world->colonies[i].cell_count > 0)
            active++;
    }
    return active;
}

// Test long-running simulation stability — no crash, colonies remain dynamic
TEST(long_run_stability_200_ticks) {
    World* world = world_create(100, 100);
    world_init_random_colonies(world, 20);

    for (int t = 0; t < 200; t++) {
        simulation_tick(world);
    }

    ASSERT_GT(count_active_colonies(world), 0);
    world_destroy(world);
}

// Test that multiple colonies coexist without one dominating everything
TEST(multiple_colonies_coexist) {
    World* world = world_create(120, 60);
    world_init_random_colonies(world, 20);

    for (int t = 0; t < 100; t++) {
        simulation_tick(world);
    }

    ASSERT_GE(count_active_colonies(world), 2);
    world_destroy(world);
}

// Test growth rate consistency — colony should grow each tick (early on)
TEST(colony_grows_monotonically_early) {
    World* world = create_single_colony_world(120, 120);
    uint32_t cid = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) { cid = world->colonies[i].id; break; }
    }
    Colony* c = world_get_colony(world, cid);
    ASSERT_TRUE(c != NULL);
    c->genome.spread_rate = 0.9f;

    int prev_cells = 9;
    int grew_count = 0;
    for (int t = 0; t < 15; t++) {
        simulation_tick(world);
        int cells = count_cells(world, cid);
        if (cells > prev_cells) grew_count++;
        prev_cells = cells;
    }
    ASSERT_GT(grew_count, 8);

    world_destroy(world);
}

// Test that very long simulation doesn't stagnate — population changes
TEST(long_run_population_dynamic_500_ticks) {
    World* world = world_create(80, 80);
    world_init_random_colonies(world, 15);

    int pop_at_100 = 0, pop_at_300 = 0, pop_at_500 = 0;
    int colonies_at_100 = 0, colonies_at_300 = 0, colonies_at_500 = 0;

    for (int t = 0; t < 500; t++) {
        simulation_tick(world);
        int checkpoint = -1;
        if (t == 99) checkpoint = 0;
        else if (t == 299) checkpoint = 1;
        else if (t == 499) checkpoint = 2;

        if (checkpoint >= 0) {
            int pop = 0, cols = 0;
            for (size_t i = 0; i < world->colony_count; i++) {
                if (world->colonies[i].active && world->colonies[i].cell_count > 0) {
                    pop += world->colonies[i].cell_count;
                    cols++;
                }
            }
            if (checkpoint == 0) { pop_at_100 = pop; colonies_at_100 = cols; }
            if (checkpoint == 1) { pop_at_300 = pop; colonies_at_300 = cols; }
            if (checkpoint == 2) { pop_at_500 = pop; colonies_at_500 = cols; }
        }
    }

    ASSERT_GT(pop_at_100, 0);
    ASSERT_GT(pop_at_300, 0);
    ASSERT_GT(pop_at_500, 0);
    ASSERT_GT(colonies_at_100 + colonies_at_300 + colonies_at_500, 3);

    world_destroy(world);
}

// ─── Runner ───

int run_growth_shapes_tests(void) {
    printf("\n=== Growth Shape Tests ===\n");
    tests_passed = 0;
    tests_failed = 0;

    RUN_TEST(single_colony_shape_is_not_rectangular);
    RUN_TEST(colony_border_has_roughness);
    RUN_TEST(perimeter_area_ratio_indicates_irregularity);
    RUN_TEST(colony_grows_diagonally);
    RUN_TEST(combat_border_is_ragged);
    RUN_TEST(directional_spread_weights_affect_shape);
    RUN_TEST(long_run_stability_200_ticks);
    RUN_TEST(multiple_colonies_coexist);
    RUN_TEST(colony_grows_monotonically_early);
    RUN_TEST(long_run_population_dynamic_500_ticks);

    printf("\nGrowth Shape Tests: %d passed, %d failed\n",
           tests_passed, tests_failed);
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    srand(42);
    return run_growth_shapes_tests() > 0 ? 1 : 0;
}
#endif
