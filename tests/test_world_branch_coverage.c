#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/server/world.h"

static int tests_passed = 0;
static int tests_failed = 0;
static int current_test_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    fflush(stdout); \
    current_test_failed = 0; \
    test_##name(); \
    if (!current_test_failed) { \
        printf("OK\n"); \
        tests_passed++; \
    } \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL\n  %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        current_test_failed = 1; \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) ASSERT((cond), #cond)
#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_NULL(ptr) ASSERT((ptr) == NULL, #ptr " is NULL")
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL, #ptr " is not NULL")

static Colony make_colony(void) {
    Colony colony;
    memset(&colony, 0, sizeof(colony));
    strncpy(colony.name, "Test colony", sizeof(colony.name) - 1);
    colony.active = true;
    return colony;
}

TEST(world_create_rejects_invalid_sizes) {
    ASSERT_NULL(world_create(0, 4));
    ASSERT_NULL(world_create(4, 0));
    ASSERT_NULL(world_create(-1, 4));
    ASSERT_NULL(world_create(4, -1));

    ASSERT_NULL(world_create(INT_MAX, 2));

    World* world = world_create(1, 1);
    ASSERT_NOT_NULL(world);
    world_destroy(world);
}

TEST(world_get_colony_checks_lookup_and_active_state) {
    World* world = world_create(4, 4);
    ASSERT_NOT_NULL(world);

    Colony colony = make_colony();
    uint32_t id = world_add_colony(world, colony);
    ASSERT_TRUE(id != 0);
    ASSERT_NOT_NULL(world_get_colony(world, id));

    world->colonies[0].active = false;
    ASSERT_NULL(world_get_colony(world, id));

    world->colonies[0].active = true;
    world->colony_by_id[id] = NULL;
    ASSERT_NULL(world_get_colony(world, id));

    ASSERT_NULL(world_get_colony(world, 0));
    ASSERT_NULL(world_get_colony(world, (uint32_t)world->colony_by_id_capacity + 10));

    world_destroy(world);
}

TEST(world_add_colony_rejects_uint32_id_overflow) {
    World* world = world_create(4, 4);
    ASSERT_NOT_NULL(world);

    atomic_store(&world->next_colony_id, UINT32_MAX);
    uint32_t id = world_add_colony(world, make_colony());
    ASSERT_EQ(id, 0u);
    ASSERT_EQ(world->colony_count, 0u);

    world_destroy(world);
}

TEST(world_colony_cell_tracking_updates_centroid_both_paths) {
    World* world = world_create(10, 10);
    ASSERT_NOT_NULL(world);

    uint32_t id = world_add_colony(world, make_colony());
    ASSERT_TRUE(id != 0);
    Colony* colony = world_get_colony(world, id);
    ASSERT_NOT_NULL(colony);

    uint32_t first = 3u * 10u + 2u;
    colony->cell_count = 1;
    world_colony_add_cell(world, id, first);
    ASSERT_EQ(colony->cell_indices_count, 1u);
    ASSERT_TRUE(colony->cell_indices_capacity >= 64u);
    ASSERT_TRUE(colony->centroid_x > 1.99f && colony->centroid_x < 2.01f);
    ASSERT_TRUE(colony->centroid_y > 2.99f && colony->centroid_y < 3.01f);

    uint32_t second = 3u * 10u + 3u;
    colony->cell_count = 2;
    world_colony_add_cell(world, id, second);
    ASSERT_EQ(colony->cell_indices_count, 2u);
    ASSERT_TRUE(colony->centroid_x > 2.49f && colony->centroid_x < 2.51f);
    ASSERT_TRUE(colony->centroid_y > 2.99f && colony->centroid_y < 3.01f);

    colony->cell_count = 2;
    world_colony_remove_cell(world, id, second);
    ASSERT_EQ(colony->cell_indices_count, 1u);
    ASSERT_TRUE(colony->centroid_x > 1.99f && colony->centroid_x < 2.01f);
    ASSERT_TRUE(colony->centroid_y > 2.99f && colony->centroid_y < 3.01f);

    colony->cell_count = 1;
    world_colony_remove_cell(world, id, first);
    ASSERT_EQ(colony->cell_indices_count, 0u);
    ASSERT_TRUE(colony->centroid_x == 0.0f);
    ASSERT_TRUE(colony->centroid_y == 0.0f);

    world_colony_add_cell(world, 0, 0);
    world_colony_remove_cell(world, 0, 0);

    world_destroy(world);
}

TEST(world_remove_colony_uses_tracked_and_fallback_clear_paths) {
    World* world = world_create(8, 8);
    ASSERT_NOT_NULL(world);

    uint32_t tracked_id = world_add_colony(world, make_colony());
    uint32_t scan_id = world_add_colony(world, make_colony());
    ASSERT_TRUE(tracked_id != 0 && scan_id != 0);

    Colony* tracked = world_get_colony(world, tracked_id);
    ASSERT_NOT_NULL(tracked);

    uint32_t tracked_a = 1u * 8u + 1u;
    uint32_t tracked_b = 1u * 8u + 2u;
    world->cells[tracked_a].colony_id = tracked_id;
    world->cells[tracked_b].colony_id = tracked_id;
    world->cells[tracked_a].age = 9;
    world->cells[tracked_b].age = 7;

    tracked->cell_count = 1;
    world_colony_add_cell(world, tracked_id, tracked_a);
    tracked->cell_count = 2;
    world_colony_add_cell(world, tracked_id, tracked_b);

    uint32_t scan_a = 6u * 8u + 6u;
    uint32_t scan_b = 6u * 8u + 7u;
    world->cells[scan_a].colony_id = scan_id;
    world->cells[scan_b].colony_id = scan_id;
    world->cells[scan_a].age = 3;
    world->cells[scan_b].age = 4;

    world_remove_colony(world, tracked_id);
    ASSERT_NULL(world_get_colony(world, tracked_id));
    ASSERT_EQ(world->cells[tracked_a].colony_id, 0u);
    ASSERT_EQ(world->cells[tracked_b].colony_id, 0u);
    ASSERT_EQ(world->cells[tracked_a].age, 0);
    ASSERT_EQ(world->cells[tracked_b].age, 0);
    ASSERT_NULL(tracked->cell_indices);
    ASSERT_EQ(tracked->cell_indices_count, 0u);

    world_remove_colony(world, scan_id);
    ASSERT_NULL(world_get_colony(world, scan_id));
    ASSERT_EQ(world->cells[scan_a].colony_id, 0u);
    ASSERT_EQ(world->cells[scan_b].colony_id, 0u);
    ASSERT_EQ(world->cells[scan_a].age, 0);
    ASSERT_EQ(world->cells[scan_b].age, 0);

    world_remove_colony(world, UINT32_MAX);
    world_remove_colony(world, 0);

    world_destroy(world);
}

int main(void) {
    printf("=== World Branch Coverage Tests ===\n");

    RUN_TEST(world_create_rejects_invalid_sizes);
    RUN_TEST(world_get_colony_checks_lookup_and_active_state);
    RUN_TEST(world_add_colony_rejects_uint32_id_overflow);
    RUN_TEST(world_colony_cell_tracking_updates_centroid_both_paths);
    RUN_TEST(world_remove_colony_uses_tracked_and_fallback_clear_paths);

    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
