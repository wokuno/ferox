/**
 * test_perf_components.c - Focused component-level performance tests
 * Isolates expensive kernels so optimization work can target them directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/shared/utils.h"
#include "../src/server/frontier_metrics.h"
#include "../src/server/world.h"
#include "../src/server/genetics.h"
#include "../src/server/simulation.h"
#include "../src/server/server.h"
#include "../src/server/threadpool.h"
#include "../src/server/atomic_sim.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s\n    At %s:%d\n", msg, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL, #ptr " is not NULL")

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static int get_perf_scale(void) {
    const char* env = getenv("FEROX_PERF_SCALE");
    if (!env || !*env) return 1;
    int scale = atoi(env);
    if (scale < 1) scale = 1;
    if (scale > 20) scale = 20;
    return scale;
}

static void print_metric(const char* name, double ms, double ops) {
    double ops_per_sec = (ms > 0.0) ? (ops * 1000.0 / ms) : 0.0;
    printf("\n    [perf] %-34s %.2f ms, %.2f ops/s\n", name, ms, ops_per_sec);
}

static World* create_seeded_world(int width, int height, int colonies, uint32_t seed) {
    World* world = world_create(width, height);
    if (!world) return NULL;
    rng_seed(seed);
    world_init_random_colonies(world, colonies);
    return world;
}

static World* create_single_colony_border_world(int width,
                                                int height,
                                                int margin,
                                                uint32_t seed) {
    World* world = world_create(width, height);
    if (!world) return NULL;

    rng_seed(seed);

    Colony colony;
    memset(&colony, 0, sizeof(colony));
    colony.genome = genome_create_random();
    colony.cell_count = 0;
    colony.active = true;
    uint32_t colony_id = world_add_colony(world, colony);
    if (colony_id == 0) {
        world_destroy(world);
        return NULL;
    }

    int start_x = margin;
    int start_y = margin;
    int end_x = width - margin;
    int end_y = height - margin;

    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell) {
                world_destroy(world);
                return NULL;
            }
            cell->colony_id = colony_id;
            cell->age = 0;
            cell->is_border = (x == start_x || x + 1 == end_x ||
                               y == start_y || y + 1 == end_y);
            world->nutrients[y * width + x] = 1.0f;
            world->toxins[y * width + x] = 0.0f;
            world->signals[y * width + x] = 0.0f;
            world->signal_source[y * width + x] = 0;
            world->colonies[colony_id - 1].cell_count++;
        }
    }

    return world;
}

static World** create_world_pool(int count,
                                 int width,
                                 int height,
                                 int colonies,
                                 uint32_t seed_base) {
    if (count <= 0) return NULL;

    World** worlds = (World**)calloc((size_t)count, sizeof(World*));
    if (!worlds) return NULL;

    for (int i = 0; i < count; i++) {
        worlds[i] = create_seeded_world(width, height, colonies, seed_base + (uint32_t)i);
        if (!worlds[i]) {
            for (int j = 0; j < i; j++) {
                world_destroy(worlds[j]);
            }
            free(worlds);
            return NULL;
        }
    }

    return worlds;
}

static void destroy_world_pool(World** worlds, int count) {
    if (!worlds) return;
    for (int i = 0; i < count; i++) {
        world_destroy(worlds[i]);
    }
    free(worlds);
}

static void evolve_worlds(World** worlds, int count, int ticks) {
    if (!worlds || ticks <= 0) return;

    for (int i = 0; i < count; i++) {
        for (int tick = 0; tick < ticks; tick++) {
            simulation_tick(worlds[i]);
        }
    }
}

TEST(simulation_update_nutrients_component_eval) {
    const int scale = get_perf_scale();
    const int iters = 80 * scale;

    World** worlds = create_world_pool(iters, 320, 180, 55, 1401u);
    ASSERT_NOT_NULL(worlds);

    for (int i = 0; i < iters; i++) {
        simulation_update_nutrients(worlds[i]);
    }

    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        simulation_update_nutrients(worlds[i]);
    }
    double elapsed = now_ms() - start;

    print_metric("simulation_update_nutrients", elapsed, (double)iters);
    ASSERT(elapsed > 0.0, "nutrient update timing must be positive");

    destroy_world_pool(worlds, iters);
}

TEST(simulation_spread_component_eval) {
    const int scale = get_perf_scale();
    const int iters = 40 * scale;

    World** worlds = create_world_pool(iters, 320, 180, 55, 2402u);
    ASSERT_NOT_NULL(worlds);

    for (int i = 0; i < iters; i++) {
        simulation_update_nutrients(worlds[i]);
    }

    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        simulation_spread(worlds[i]);
    }
    double elapsed = now_ms() - start;

    print_metric("simulation_spread", elapsed, (double)iters);
    ASSERT(elapsed > 0.0, "spread timing must be positive");

    destroy_world_pool(worlds, iters);
}

TEST(simulation_update_scents_component_eval) {
    const int scale = get_perf_scale();
    const int iters = 80 * scale;

    World** worlds = create_world_pool(iters, 320, 180, 55, 2803u);
    ASSERT_NOT_NULL(worlds);

    for (int i = 0; i < iters; i++) {
        simulation_update_scents(worlds[i]);
    }

    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        simulation_update_scents(worlds[i]);
    }
    double elapsed = now_ms() - start;

    print_metric("simulation_update_scents", elapsed, (double)iters);
    ASSERT(elapsed > 0.0, "scent update timing must be positive");

    destroy_world_pool(worlds, iters);
}

TEST(simulation_resolve_combat_component_eval) {
    const int scale = get_perf_scale();
    const int iters = 20 * scale;

    World** worlds = create_world_pool(iters, 240, 140, 90, 3204u);
    ASSERT_NOT_NULL(worlds);
    evolve_worlds(worlds, iters, 6);

    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        simulation_resolve_combat(worlds[i]);
    }
    double elapsed = now_ms() - start;

    print_metric("simulation_resolve_combat", elapsed, (double)iters);
    ASSERT(elapsed > 0.0, "combat timing must be positive");

    destroy_world_pool(worlds, iters);
}

TEST(simulation_resolve_combat_sparse_border_component_eval) {
    const int scale = get_perf_scale();
    const int iters = 20 * scale;

    World** worlds = (World**)calloc((size_t)iters, sizeof(World*));
    ASSERT_NOT_NULL(worlds);

    for (int i = 0; i < iters; i++) {
        worlds[i] = create_single_colony_border_world(240, 140, 18, 4205u + (uint32_t)i);
        ASSERT_NOT_NULL(worlds[i]);
    }

    for (int i = 0; i < iters; i++) {
        simulation_resolve_combat(worlds[i]);
    }

    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        simulation_resolve_combat(worlds[i]);
    }
    double elapsed = now_ms() - start;

    print_metric("simulation_resolve_combat_sparse_border", elapsed, (double)iters);
    ASSERT(elapsed > 0.0, "sparse-border combat timing must be positive");

    destroy_world_pool(worlds, iters);
}

TEST(frontier_telemetry_component_eval) {
    const int scale = get_perf_scale();
    const int iters = 40 * scale;

    World** worlds = create_world_pool(iters, 320, 180, 55, 3605u);
    ASSERT_NOT_NULL(worlds);
    evolve_worlds(worlds, iters, 10);

    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        FrontierTelemetry sample;
        ASSERT(frontier_telemetry_compute(worlds[i], (uint32_t)(3605u + i), &sample),
               "frontier telemetry computed");
    }
    double elapsed = now_ms() - start;

    print_metric("frontier_telemetry_compute", elapsed, (double)iters);
    ASSERT(elapsed > 0.0, "frontier telemetry timing must be positive");

    destroy_world_pool(worlds, iters);
}

TEST(atomic_spread_phase_component_eval) {
    const int scale = get_perf_scale();
    const int iters = 60 * scale;

    World* world = create_seeded_world(320, 180, 55, 3403u);
    ASSERT_NOT_NULL(world);

    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    AtomicWorld* aworld = atomic_world_create(world, pool, 4);
    ASSERT_NOT_NULL(aworld);

    for (int i = 0; i < 4; i++) {
        atomic_age(aworld);
        atomic_barrier(aworld);
        atomic_spread(aworld);
        atomic_barrier(aworld);
        atomic_world_sync_to_world(aworld);
        atomic_world_sync_from_world(aworld);
    }

    double age_start = now_ms();
    for (int i = 0; i < iters; i++) {
        atomic_age(aworld);
        atomic_barrier(aworld);
    }
    double age_ms = now_ms() - age_start;

    double spread_start = now_ms();
    for (int i = 0; i < iters; i++) {
        atomic_spread(aworld);
        atomic_barrier(aworld);
    }
    double spread_ms = now_ms() - spread_start;

    print_metric("atomic_age phase", age_ms, (double)iters);
    print_metric("atomic_spread phase", spread_ms, (double)iters);
    ASSERT(age_ms > 0.0 && spread_ms > 0.0, "atomic phase timings must be positive");

    atomic_world_destroy(aworld);
    threadpool_destroy(pool);
    world_destroy(world);
}

TEST(snapshot_build_component_eval) {
    const int scale = get_perf_scale();
    const int iters = 160 * scale;

    Server* server = server_create_headless(320, 180, 4);
    ASSERT_NOT_NULL(server);

    rng_seed(4404u);
    world_init_random_colonies(server->world, 72);
    for (int i = 0; i < 3; i++) {
        simulation_tick(server->world);
    }

    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        proto_world snapshot;
        ASSERT(server_build_protocol_world_snapshot(server->world,
                                                   server->paused,
                                                   server->speed_multiplier,
                                                   &snapshot) == 0,
               "snapshot build succeeds");
        proto_world_free(&snapshot);
    }
    double elapsed = now_ms() - start;

    print_metric("server snapshot build", elapsed, (double)iters);
    ASSERT(elapsed > 0.0, "snapshot timing must be positive");

    server_destroy(server);
}

int run_perf_component_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("\n=== Performance Component Tests ===\n");
    printf("    (Set FEROX_PERF_SCALE=2..20 for heavier benchmark loops)\n\n");

    RUN_TEST(simulation_update_nutrients_component_eval);
    RUN_TEST(simulation_spread_component_eval);
    RUN_TEST(simulation_update_scents_component_eval);
    RUN_TEST(simulation_resolve_combat_component_eval);
    RUN_TEST(simulation_resolve_combat_sparse_border_component_eval);
    RUN_TEST(frontier_telemetry_component_eval);
    RUN_TEST(atomic_spread_phase_component_eval);
    RUN_TEST(snapshot_build_component_eval);

    printf("\n--- Performance Component Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_perf_component_tests() > 0 ? 1 : 0;
}
#endif
