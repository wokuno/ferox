/**
 * test_performance_eval.c - Broad performance evaluation tests
 * Times core paths across shared, protocol, world, simulation, and threading.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "../src/shared/types.h"
#include "../src/shared/utils.h"
#include "../src/shared/names.h"
#include "../src/shared/colors.h"
#include "../src/shared/protocol.h"
#include "../src/server/world.h"
#include "../src/server/genetics.h"
#include "../src/server/frontier_metrics.h"
#include "../src/server/simulation.h"
#include "../src/server/server.h"
#include "../src/server/threadpool.h"
#include "../src/server/atomic_sim.h"

// Test framework
static int tests_passed = 0;
static int tests_failed = 0;

#define PERF_DEFAULT_WIDTH 400
#define PERF_DEFAULT_HEIGHT 200
#define PERF_DEFAULT_COLONIES 50

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
    printf("\n    [perf] %-30s %.2f ms, %.2f ops/s\n", name, ms, ops_per_sec);
}

static void print_cells_throughput(const char* name, double ms, double cells) {
    double cells_per_sec = (ms > 0.0) ? (cells * 1000.0 / ms) : 0.0;
    printf("    [perf] %s: %.2f Mcells/s\n", name, cells_per_sec / 1000000.0);
}

static World* create_seeded_world(int width, int height, int colonies, uint32_t seed) {
    World* world = world_create(width, height);
    if (!world) return NULL;
    rng_seed(seed);
    world_init_random_colonies(world, colonies);
    return world;
}

static atomic_int perf_task_counter;
static void perf_increment_task(void* arg) {
    (void)arg;
    atomic_fetch_add(&perf_task_counter, 1);
}

typedef struct {
    int iterations;
} PerfTaskBatch;

typedef struct {
    ThreadPool* pool;
    atomic_int remaining;
} PerfFollowOnChainState;

static void perf_increment_task_batch(void* arg) {
    PerfTaskBatch* batch = (PerfTaskBatch*)arg;
    for (int i = 0; i < batch->iterations; i++) {
        atomic_fetch_add(&perf_task_counter, 1);
    }
}

static void perf_follow_on_increment_task(void* arg) {
    PerfFollowOnChainState* state = (PerfFollowOnChainState*)arg;
    atomic_fetch_add(&perf_task_counter, 1);

    if (atomic_fetch_sub(&state->remaining, 1) > 0) {
        threadpool_submit(state->pool, perf_follow_on_increment_task, state);
    }
}

TEST(shared_name_color_generation_throughput) {
    const int scale = get_perf_scale();
    const int iters = 50000 * scale;
    char name[64];
    volatile int sink = 0;

    rng_seed(2026);
    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        generate_scientific_name(name, sizeof(name));
        Color body = generate_body_color();
        Color border = generate_border_color(body);
        sink += (int)name[0] + body.r + border.g;
    }
    double elapsed = now_ms() - start;
    (void)sink;

    print_metric("name+color generation", elapsed, (double)iters);

    double ns_per_iter = (elapsed * 1000000.0) / (double)iters;
    ASSERT(ns_per_iter < 50000.0, "shared generator path is unexpectedly slow");
}

TEST(world_lifecycle_throughput) {
    const int scale = get_perf_scale();
    const int iters = 180 * scale;

    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        World* world = create_seeded_world(200, 120, 20, (uint32_t)(1000 + i));
        ASSERT_NOT_NULL(world);
        simulation_update_nutrients(world);
        world_destroy(world);
    }
    double elapsed = now_ms() - start;

    print_metric("world create/init/destroy", elapsed, (double)iters);

    double ns_per_iter = (elapsed * 1000000.0) / (double)iters;
    ASSERT(ns_per_iter < 20000000.0, "world lifecycle path is unexpectedly slow");
}

TEST(protocol_world_serialize_deserialize_throughput) {
    const int scale = get_perf_scale();
    const int iters = 120 * scale;

    proto_world world;
    proto_world_init(&world);
    world.width = 300;
    world.height = 160;
    world.tick = 12345;
    world.colony_count = 64;
    world.paused = false;
    world.speed_multiplier = 1.0f;
    proto_world_alloc_grid(&world, world.width, world.height);
    ASSERT_NOT_NULL(world.grid);

    for (uint32_t i = 0; i < world.colony_count; i++) {
        world.colonies[i].id = i + 1;
        snprintf(world.colonies[i].name, MAX_COLONY_NAME, "PerfColony%u", i + 1);
        world.colonies[i].x = (float)(i % world.width);
        world.colonies[i].y = (float)(i % world.height);
        world.colonies[i].radius = 8.0f + (float)(i % 5);
        world.colonies[i].population = 100 + i * 7;
        world.colonies[i].max_population = world.colonies[i].population + 50;
        world.colonies[i].growth_rate = 0.5f;
        world.colonies[i].color_r = (uint8_t)(i * 13);
        world.colonies[i].color_g = (uint8_t)(i * 23);
        world.colonies[i].color_b = (uint8_t)(i * 31);
        world.colonies[i].alive = true;
    }

    for (uint32_t i = 0; i < world.grid_size; i++) {
        world.grid[i] = (uint16_t)((i % 19 == 0) ? (i % world.colony_count) + 1 : 0);
    }

    size_t total_bytes = 0;
    double start = now_ms();
    for (int i = 0; i < iters; i++) {
        uint8_t* buf = NULL;
        size_t len = 0;
        int rc = protocol_serialize_world_state(&world, &buf, &len);
        ASSERT_EQ(rc, 0);
        ASSERT_NOT_NULL(buf);
        total_bytes += len;

        proto_world decoded;
        proto_world_init(&decoded);
        rc = protocol_deserialize_world_state(buf, len, &decoded);
        ASSERT_EQ(rc, 0);

        proto_world_free(&decoded);
        free(buf);
    }
    double elapsed = now_ms() - start;

    print_metric("protocol serialize+deserialize", elapsed, (double)iters);
    double mb_per_sec = (elapsed > 0.0) ? ((double)total_bytes / (1024.0 * 1024.0)) * 1000.0 / elapsed : 0.0;
    printf("    [perf] protocol throughput: %.2f MB/s\n", mb_per_sec);

    double ns_per_iter = (elapsed * 1000000.0) / (double)iters;
    ASSERT(ns_per_iter < 50000000.0, "protocol world serialization path is unexpectedly slow");

    proto_world_free(&world);
}

TEST(protocol_world_path_breakdown_eval) {
    const int scale = get_perf_scale();
    const int iters = 120 * scale;

    proto_world world;
    proto_world_init(&world);
    world.width = 320;
    world.height = 180;
    world.tick = 54321;
    world.colony_count = 72;
    world.paused = false;
    world.speed_multiplier = 1.0f;
    proto_world_alloc_grid(&world, world.width, world.height);
    ASSERT_NOT_NULL(world.grid);

    for (uint32_t i = 0; i < world.colony_count; i++) {
        world.colonies[i].id = i + 1;
        snprintf(world.colonies[i].name, MAX_COLONY_NAME, "Breakdown%u", i + 1);
        world.colonies[i].x = (float)(i % world.width);
        world.colonies[i].y = (float)(i % world.height);
        world.colonies[i].radius = 10.0f + (float)(i % 7);
        world.colonies[i].population = 200 + i * 5;
        world.colonies[i].max_population = world.colonies[i].population + 60;
        world.colonies[i].growth_rate = 0.45f;
        world.colonies[i].alive = true;
    }

    for (uint32_t i = 0; i < world.grid_size; i++) {
        world.grid[i] = (uint16_t)((i % 17 == 0) ? (i % world.colony_count) + 1 : 0);
    }

    uint8_t* encoded = NULL;
    size_t encoded_len = 0;
    int rc = protocol_serialize_world_state(&world, &encoded, &encoded_len);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(encoded);

    double ser_start = now_ms();
    for (int i = 0; i < iters; i++) {
        uint8_t* buf = NULL;
        size_t len = 0;
        rc = protocol_serialize_world_state(&world, &buf, &len);
        ASSERT_EQ(rc, 0);
        ASSERT_NOT_NULL(buf);
        free(buf);
    }
    double ser_ms = now_ms() - ser_start;

    double de_start = now_ms();
    for (int i = 0; i < iters; i++) {
        proto_world decoded;
        proto_world_init(&decoded);
        rc = protocol_deserialize_world_state(encoded, encoded_len, &decoded);
        ASSERT_EQ(rc, 0);
        proto_world_free(&decoded);
    }
    double de_ms = now_ms() - de_start;

    print_metric("protocol serialize only", ser_ms, (double)iters);
    print_metric("protocol deserialize only", de_ms, (double)iters);
    printf("    [perf] protocol size per frame: %zu bytes\n", encoded_len);

    if (ser_ms > de_ms * 1.5) {
        printf("    [hint] serialization dominates; consider buffer reuse or layout simplification\n");
    } else if (de_ms > ser_ms * 1.5) {
        printf("    [hint] deserialization dominates; consider faster bounds checks or memcpy strategy\n");
    } else {
        printf("    [hint] serialize/deserialize costs are balanced\n");
    }

    ASSERT(ser_ms > 0.0 && de_ms > 0.0, "protocol timing must be positive");

    free(encoded);
    proto_world_free(&world);
}

TEST(server_broadcast_path_breakdown_eval) {
    const int scale = get_perf_scale();
    const int iters = 80 * scale;

    Server* server = server_create_headless(PERF_DEFAULT_WIDTH, PERF_DEFAULT_HEIGHT, 4);
    ASSERT_NOT_NULL(server);

    rng_seed(1337);
    world_init_random_colonies(server->world, PERF_DEFAULT_COLONIES);
    for (int i = 0; i < 3; i++) {
        simulation_tick(server->world);
    }

    size_t total_snapshot_bytes = 0;
    double build_start = now_ms();
    for (int i = 0; i < iters; i++) {
        proto_world snapshot;
        ASSERT_EQ(server_build_protocol_world_snapshot(server->world,
                                                      server->paused,
                                                      server->speed_multiplier,
                                                      &snapshot),
                  0);
        total_snapshot_bytes += (size_t)snapshot.colony_count * COLONY_SERIALIZED_SIZE;
        total_snapshot_bytes += (size_t)snapshot.grid_size * sizeof(uint16_t);
        proto_world_free(&snapshot);
    }
    double build_ms = now_ms() - build_start;

    size_t total_encoded_bytes = 0;
    double build_serialize_start = now_ms();
    for (int i = 0; i < iters; i++) {
        proto_world snapshot;
        ASSERT_EQ(server_build_protocol_world_snapshot(server->world,
                                                      server->paused,
                                                      server->speed_multiplier,
                                                      &snapshot),
                  0);

        uint8_t* encoded = NULL;
        size_t encoded_len = 0;
        int rc = protocol_serialize_world_state(&snapshot, &encoded, &encoded_len);
        ASSERT_EQ(rc, 0);
        ASSERT_NOT_NULL(encoded);

        total_encoded_bytes += encoded_len;

        free(encoded);
        proto_world_free(&snapshot);
    }
    double build_serialize_ms = now_ms() - build_serialize_start;

    double broadcast_start = now_ms();
    for (int i = 0; i < iters; i++) {
        server_broadcast_world_state(server);
    }
    double broadcast_ms = now_ms() - broadcast_start;

    print_metric("broadcast build snapshot", build_ms, (double)iters);
    print_metric("broadcast build+serialize", build_serialize_ms, (double)iters);
    print_metric("broadcast end-to-end (0 clients)", broadcast_ms, (double)iters);

    double avg_snapshot_kb = (double)total_snapshot_bytes / (double)iters / 1024.0;
    double avg_encoded_kb = (double)total_encoded_bytes / (double)iters / 1024.0;
    printf("    [perf] broadcast avg snapshot: %.2f KiB, encoded: %.2f KiB\n", avg_snapshot_kb, avg_encoded_kb);

    if (build_serialize_ms > 0.0) {
        double build_share = (build_ms / build_serialize_ms) * 100.0;
        printf("    [perf] broadcast stage share: build=%.1f%% serialize+other=%.1f%%\n",
               build_share, 100.0 - build_share);
    }

    ASSERT(build_ms > 0.0 && build_serialize_ms > 0.0 && broadcast_ms > 0.0,
           "broadcast timings must be positive");

    server_destroy(server);
}

TEST(simulation_tick_throughput) {
    const int scale = get_perf_scale();
    const int ticks = 35 * scale;

    World* world = create_seeded_world(
        PERF_DEFAULT_WIDTH, PERF_DEFAULT_HEIGHT, PERF_DEFAULT_COLONIES, 4242u);
    ASSERT_NOT_NULL(world);

    for (int i = 0; i < 5; i++) simulation_tick(world);  // warm-up

    double start = now_ms();
    for (int i = 0; i < ticks; i++) simulation_tick(world);
    double elapsed = now_ms() - start;

    print_metric("simulation_tick (serial)", elapsed, (double)ticks);

    double avg_tick_ms = elapsed / (double)ticks;
    ASSERT(avg_tick_ms < 200.0, "simulation_tick average too slow");

    world_destroy(world);
}

TEST(frontier_telemetry_seeded_run_eval) {
    const int scale = get_perf_scale();
    const int ticks = 30 * scale;
    const uint32_t seed = 48048u;

    World* world = create_seeded_world(
        PERF_DEFAULT_WIDTH, PERF_DEFAULT_HEIGHT, PERF_DEFAULT_COLONIES, seed);
    ASSERT_NOT_NULL(world);

    FrontierTelemetry sample;
    char telemetry_line[256];
    double start = now_ms();

    printf("    [perf] frontier telemetry csv: seed,tick,frontier_sector_count,lineage_diversity_proxy,lineage_entropy_bits\n");

    for (int i = 0; i < ticks; i++) {
        simulation_tick(world);
        ASSERT(frontier_telemetry_compute(world, seed, &sample), "frontier telemetry computed");

        ASSERT(sample.frontier_sector_count <= FRONTIER_TELEMETRY_SECTORS,
               "frontier sector count should be bounded");
        ASSERT(sample.lineage_diversity_proxy >= 0.0f && sample.lineage_diversity_proxy <= 1.0f,
               "lineage diversity proxy should be normalized");
        ASSERT(sample.lineage_entropy_bits >= 0.0f,
               "lineage entropy should be non-negative");

        if ((i + 1) % 10 == 0) {
            ASSERT(frontier_telemetry_format_logfmt(&sample, telemetry_line, sizeof(telemetry_line)) > 0,
                   "telemetry line formatting succeeds");
            printf("    [perf] frontier_telemetry %s\n", telemetry_line);
        }
    }

    double elapsed = now_ms() - start;
    print_metric("frontier telemetry compute", elapsed, (double)ticks);

    world_destroy(world);
}

TEST(atomic_tick_throughput_and_speedup_eval) {
    const int scale = get_perf_scale();
    const int ticks = 30 * scale;

    World* serial_world = create_seeded_world(
        PERF_DEFAULT_WIDTH, PERF_DEFAULT_HEIGHT, PERF_DEFAULT_COLONIES, 5252u);
    ASSERT_NOT_NULL(serial_world);
    World* parallel_world = create_seeded_world(
        PERF_DEFAULT_WIDTH, PERF_DEFAULT_HEIGHT, PERF_DEFAULT_COLONIES, 5252u);
    ASSERT_NOT_NULL(parallel_world);

    // Serial baseline
    for (int i = 0; i < 4; i++) simulation_tick(serial_world);
    double serial_start = now_ms();
    for (int i = 0; i < ticks; i++) simulation_tick(serial_world);
    double serial_ms = now_ms() - serial_start;

    // Atomic parallel
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    AtomicWorld* aworld = atomic_world_create(parallel_world, pool, 4);
    ASSERT_NOT_NULL(aworld);
    for (int i = 0; i < 4; i++) atomic_tick(aworld);
    double atomic_start = now_ms();
    for (int i = 0; i < ticks; i++) atomic_tick(aworld);
    double atomic_ms = now_ms() - atomic_start;

    print_metric("atomic_tick (4 threads)", atomic_ms, (double)ticks);
    print_metric("simulation_tick baseline", serial_ms, (double)ticks);
    printf("    [perf] atomic/serial time ratio: %.2fx\n",
           serial_ms > 0.0 ? atomic_ms / serial_ms : 0.0);

    // Keep this very loose: CI hosts vary a lot, especially under coverage.
    if (atomic_ms > serial_ms * 8.0 + 5.0) {
        printf("    [hint] atomic path is slower than expected on this host\n");
    }
    ASSERT(atomic_ms <= serial_ms * 30.0 + 5.0, "atomic_tick path regressed severely");

    atomic_world_destroy(aworld);
    threadpool_destroy(pool);
    world_destroy(serial_world);
    world_destroy(parallel_world);
}

TEST(threadpool_task_throughput) {
    const int scale = get_perf_scale();
    const int tasks = 50000 * scale;

    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);

    atomic_store(&perf_task_counter, 0);

    double start = now_ms();
    for (int i = 0; i < tasks; i++) {
        threadpool_submit(pool, perf_increment_task, NULL);
    }
    threadpool_wait(pool);
    double elapsed = now_ms() - start;

    ASSERT_EQ(atomic_load(&perf_task_counter), tasks);

    print_metric("threadpool submit+execute", elapsed, (double)tasks);

    double ns_per_task = (elapsed * 1000000.0) / (double)tasks;
    ASSERT(ns_per_task < 2000000.0, "threadpool path is unexpectedly slow");

    threadpool_destroy(pool);
}

TEST(atomic_tick_thread_scaling_eval) {
    const int scale = get_perf_scale();
    const int ticks = 20 * scale;
    const int thread_options[3] = {1, 2, 4};
    double elapsed_ms[3] = {0.0, 0.0, 0.0};

    for (int idx = 0; idx < 3; idx++) {
        int threads = thread_options[idx];
        World* world = create_seeded_world(
            PERF_DEFAULT_WIDTH, PERF_DEFAULT_HEIGHT, PERF_DEFAULT_COLONIES, 9000u);
        ASSERT_NOT_NULL(world);

        ThreadPool* pool = threadpool_create(threads);
        ASSERT_NOT_NULL(pool);
        AtomicWorld* aworld = atomic_world_create(world, pool, threads);
        ASSERT_NOT_NULL(aworld);

        for (int i = 0; i < 4; i++) atomic_tick(aworld);

        double start = now_ms();
        for (int i = 0; i < ticks; i++) atomic_tick(aworld);
        elapsed_ms[idx] = now_ms() - start;

        print_metric(threads == 1 ? "atomic_tick (1 thread)" :
                     (threads == 2 ? "atomic_tick (2 threads)" : "atomic_tick (4 threads)"),
                     elapsed_ms[idx], (double)ticks);

        atomic_world_destroy(aworld);
        threadpool_destroy(pool);
        world_destroy(world);
    }

    double base = elapsed_ms[0];
    double speedup_2 = base > 0.0 ? base / elapsed_ms[1] : 0.0;
    double speedup_4 = base > 0.0 ? base / elapsed_ms[2] : 0.0;

    printf("    [perf] speedup vs 1-thread: 2-thread=%.2fx 4-thread=%.2fx\n", speedup_2, speedup_4);
    if (speedup_4 < 1.0) {
        printf("    [hint] 4-thread slower than 1-thread; inspect scheduling overhead and false sharing\n");
    }

    ASSERT(elapsed_ms[2] <= elapsed_ms[0] * 8.0 + 5.0, "4-thread atomic path regressed severely");
}

TEST(atomic_tick_phase_breakdown_eval) {
    const int scale = get_perf_scale();
    const int ticks = 25 * scale;

    World* world = create_seeded_world(
        PERF_DEFAULT_WIDTH, PERF_DEFAULT_HEIGHT, PERF_DEFAULT_COLONIES, 6006u);
    ASSERT_NOT_NULL(world);

    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    AtomicWorld* aworld = atomic_world_create(world, pool, 4);
    ASSERT_NOT_NULL(aworld);

    for (int i = 0; i < 3; i++) {
        atomic_tick(aworld);
    }

    double age_ms = 0.0;
    double spread_ms = 0.0;
    double sync_to_ms = 0.0;
    double serial_ms = 0.0;
    double sync_from_ms = 0.0;
    double total_ms = 0.0;

    for (int i = 0; i < ticks; i++) {
        AtomicTickBreakdown breakdown;
        atomic_tick_with_breakdown(aworld, &breakdown);
        age_ms += breakdown.age_ms;
        spread_ms += breakdown.spread_ms;
        sync_to_ms += breakdown.sync_to_world_ms;
        serial_ms += breakdown.serial_ms;
        sync_from_ms += breakdown.sync_from_world_ms;
        total_ms += breakdown.total_ms;
    }

    print_metric("atomic phase: age", age_ms, (double)ticks);
    print_metric("atomic phase: spread", spread_ms, (double)ticks);
    print_metric("atomic phase: sync_to_world", sync_to_ms, (double)ticks);
    print_metric("atomic phase: serial core", serial_ms, (double)ticks);
    print_metric("atomic phase: sync_from_world", sync_from_ms, (double)ticks);
    print_metric("atomic phase: total", total_ms, (double)ticks);

    double measured_sum_ms = age_ms + spread_ms + sync_to_ms + serial_ms + sync_from_ms;
    double overhead_ms = total_ms - measured_sum_ms;
    if (overhead_ms < 0.0) overhead_ms = 0.0;

    if (total_ms > 0.0) {
        printf("    [perf] atomic phase share: age=%.1f%% spread=%.1f%% sync_to=%.1f%% serial=%.1f%% sync_from=%.1f%% overhead=%.1f%%\n",
               age_ms * 100.0 / total_ms,
               spread_ms * 100.0 / total_ms,
               sync_to_ms * 100.0 / total_ms,
               serial_ms * 100.0 / total_ms,
               sync_from_ms * 100.0 / total_ms,
               overhead_ms * 100.0 / total_ms);
    }

    ASSERT(total_ms > 0.0, "atomic phase timing must be positive");
    ASSERT(sync_to_ms > 0.0 && sync_from_ms > 0.0, "atomic sync timings must be positive");

    atomic_world_destroy(aworld);
    threadpool_destroy(pool);
    world_destroy(world);
}

TEST(atomic_sync_throughput_scaling_eval) {
    const int scale = get_perf_scale();
    const int iters = 40 * scale;
    const int sizes[3][2] = {
        {200, 100},
        {400, 200},
        {800, 400}
    };
    double sync_to_cells_per_sec[3] = {0.0, 0.0, 0.0};
    double sync_from_cells_per_sec[3] = {0.0, 0.0, 0.0};

    for (int i = 0; i < 3; i++) {
        const int width = sizes[i][0];
        const int height = sizes[i][1];
        const int colonies = (width * height) / 1500;

        World* world = create_seeded_world(width, height, colonies > 12 ? colonies : 12, (uint32_t)(7000 + i));
        ASSERT_NOT_NULL(world);

        ThreadPool* pool = threadpool_create(4);
        ASSERT_NOT_NULL(pool);
        AtomicWorld* aworld = atomic_world_create(world, pool, 4);
        ASSERT_NOT_NULL(aworld);

        for (int warm = 0; warm < 3; warm++) {
            atomic_world_sync_to_world(aworld);
            atomic_world_sync_from_world(aworld);
        }

        double to_start = now_ms();
        for (int iter = 0; iter < iters; iter++) {
            atomic_world_sync_to_world(aworld);
        }
        double sync_to_ms = now_ms() - to_start;

        double from_start = now_ms();
        for (int iter = 0; iter < iters; iter++) {
            atomic_world_sync_from_world(aworld);
        }
        double sync_from_ms = now_ms() - from_start;

        const double total_cells = (double)width * (double)height * (double)iters;
        sync_to_cells_per_sec[i] = (sync_to_ms > 0.0) ? (total_cells * 1000.0 / sync_to_ms) : 0.0;
        sync_from_cells_per_sec[i] = (sync_from_ms > 0.0) ? (total_cells * 1000.0 / sync_from_ms) : 0.0;

        char label_to[64];
        char label_from[64];
        snprintf(label_to, sizeof(label_to), "sync_to_world %dx%d", width, height);
        snprintf(label_from, sizeof(label_from), "sync_from_world %dx%d", width, height);
        print_metric(label_to, sync_to_ms, (double)iters);
        print_metric(label_from, sync_from_ms, (double)iters);
        print_cells_throughput(label_to, sync_to_ms, total_cells);
        print_cells_throughput(label_from, sync_from_ms, total_cells);

        ASSERT(sync_to_ms > 0.0 && sync_from_ms > 0.0, "sync timings must be positive");

        atomic_world_destroy(aworld);
        threadpool_destroy(pool);
        world_destroy(world);
    }

    if (sync_to_cells_per_sec[0] > 0.0 && sync_from_cells_per_sec[0] > 0.0) {
        printf("    [perf] sync_to scaling vs 200x100: 400x200=%.2fx 800x400=%.2fx\n",
               sync_to_cells_per_sec[1] / sync_to_cells_per_sec[0],
               sync_to_cells_per_sec[2] / sync_to_cells_per_sec[0]);
        printf("    [perf] sync_from scaling vs 200x100: 400x200=%.2fx 800x400=%.2fx\n",
               sync_from_cells_per_sec[1] / sync_from_cells_per_sec[0],
               sync_from_cells_per_sec[2] / sync_from_cells_per_sec[0]);
    }
}

TEST(threadpool_granularity_eval) {
    const int scale = get_perf_scale();
    const int total_increments = 50000 * scale;
    const int chunk = 250;
    const int submit_chunk = 256;
    const int batch_tasks = total_increments / chunk;

    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);

    void* tiny_args[submit_chunk];
    for (int i = 0; i < submit_chunk; i++) {
        tiny_args[i] = NULL;
    }

    atomic_store(&perf_task_counter, 0);
    double tiny_start = now_ms();
    for (int i = 0; i < total_increments; i++) {
        threadpool_submit(pool, perf_increment_task, NULL);
    }
    threadpool_wait(pool);
    double tiny_ms = now_ms() - tiny_start;
    ASSERT_EQ(atomic_load(&perf_task_counter), total_increments);

    atomic_store(&perf_task_counter, 0);
    double chunked_submit_start = now_ms();
    for (int i = 0; i < total_increments; i += submit_chunk) {
        int count = total_increments - i;
        if (count > submit_chunk) {
            count = submit_chunk;
        }
        threadpool_submit_batch(pool, perf_increment_task, tiny_args, count);
    }
    threadpool_wait(pool);
    double chunked_submit_ms = now_ms() - chunked_submit_start;
    ASSERT_EQ(atomic_load(&perf_task_counter), total_increments);

    PerfTaskBatch* batches = (PerfTaskBatch*)calloc((size_t)batch_tasks, sizeof(PerfTaskBatch));
    ASSERT_NOT_NULL(batches);
    for (int i = 0; i < batch_tasks; i++) {
        batches[i].iterations = chunk;
    }

    atomic_store(&perf_task_counter, 0);
    double batched_start = now_ms();
    for (int i = 0; i < batch_tasks; i++) {
        threadpool_submit(pool, perf_increment_task_batch, &batches[i]);
    }
    threadpool_wait(pool);
    double batched_ms = now_ms() - batched_start;
    ASSERT_EQ(atomic_load(&perf_task_counter), total_increments);

    print_metric("threadpool tiny tasks", tiny_ms, (double)total_increments);
    print_metric("threadpool chunked submit", chunked_submit_ms, (double)total_increments);
    print_metric("threadpool batched tasks", batched_ms, (double)total_increments);

    if (chunked_submit_ms > 0.0) {
        printf("    [perf] tiny/chunked-submit ratio: %.2fx\n", tiny_ms / chunked_submit_ms);
    }
    if (batched_ms > 0.0) {
        printf("    [perf] tiny/batched ratio: %.2fx\n", tiny_ms / batched_ms);
        if (tiny_ms / batched_ms > 2.0) {
            printf("    [hint] task submission overhead is high; consider coarser work units\n");
        }
    }

    ASSERT(chunked_submit_ms <= tiny_ms * 4.0 + 2.0,
           "chunked submit path regressed severely");

    free(batches);
    threadpool_destroy(pool);
}

TEST(threadpool_worker_follow_on_submit_eval) {
    const int scale = get_perf_scale();
    const int total_increments = 50000 * scale;

    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);

    atomic_store(&perf_task_counter, 0);
    PerfFollowOnChainState state = {
        .pool = pool,
        .remaining = ATOMIC_VAR_INIT(total_increments - 1),
    };

    double start = now_ms();
    threadpool_submit(pool, perf_follow_on_increment_task, &state);
    threadpool_wait(pool);
    double elapsed = now_ms() - start;

    ASSERT_EQ(atomic_load(&perf_task_counter), total_increments);
    print_metric("threadpool worker follow-on", elapsed, (double)total_increments);

    threadpool_destroy(pool);
}

int run_performance_eval_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("\n=== Performance Eval Tests ===\n");
    printf("    (Set FEROX_PERF_SCALE=2..20 for heavier benchmark loops)\n\n");

    RUN_TEST(shared_name_color_generation_throughput);
    RUN_TEST(world_lifecycle_throughput);
    RUN_TEST(protocol_world_serialize_deserialize_throughput);
    RUN_TEST(protocol_world_path_breakdown_eval);
    RUN_TEST(server_broadcast_path_breakdown_eval);
    RUN_TEST(simulation_tick_throughput);
    RUN_TEST(frontier_telemetry_seeded_run_eval);
    RUN_TEST(atomic_tick_throughput_and_speedup_eval);
    RUN_TEST(atomic_tick_thread_scaling_eval);
    RUN_TEST(atomic_tick_phase_breakdown_eval);
    RUN_TEST(atomic_sync_throughput_scaling_eval);
    RUN_TEST(threadpool_task_throughput);
    RUN_TEST(threadpool_granularity_eval);
    RUN_TEST(threadpool_worker_follow_on_submit_eval);

    printf("\n--- Performance Eval Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_performance_eval_tests() > 0 ? 1 : 0;
}
#endif
