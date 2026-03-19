#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#include "../src/shared/types.h"
#include "../src/shared/protocol.h"
#include "../src/shared/utils.h"
#include "../src/server/world.h"
#include "../src/server/genetics.h"
#include "../src/server/simulation.h"
#include "../src/server/server.h"
#include "../src/server/threadpool.h"
#include "../src/server/atomic_sim.h"

static const char* perf_arch_name(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__)
    return "aarch64";
#elif defined(__arm__)
    return "arm";
#else
    return "unknown";
#endif
}

static const char* perf_atomic_lane_name(void) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    return "x86_tso";
#elif defined(__aarch64__) || defined(__arm__)
    return "arm_weak_ordering";
#else
    return "generic";
#endif
}

static bool perf_atomic_lane_is_x86(void) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    return true;
#else
    return false;
#endif
}

static bool perf_atomic_lane_is_arm(void) {
#if defined(__aarch64__) || defined(__arm__)
    return true;
#else
    return false;
#endif
}

static void profile_platform_characteristics(void) {
    const char* arch = perf_arch_name();

    printf("[platform] arch: %s, cacheline target: %d, stats align: %zu, stats size: %zu\n",
           arch,
           FEROX_CACHELINE_SIZE,
           (size_t)_Alignof(AtomicColonyStats),
           sizeof(AtomicColonyStats));

    atomic_uint_fast64_t v;
    atomic_init(&v, 0);
    printf("[platform] uint_fast64 lock-free: %d\n", atomic_is_lock_free(&v) ? 1 : 0);
}

static volatile uint64_t perf_sink = 0;

#define ATOMIC_MICROBENCH_STRIPES 64

static int get_perf_scale(void) {
    const char* env = getenv("FEROX_PERF_SCALE");
    if (!env || !*env) {
        return 1;
    }

    int scale = atoi(env);
    if (scale < 1) {
        scale = 1;
    }
    if (scale > 10) {
        scale = 10;
    }
    return scale;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static double ns_to_ms(uint64_t ns) {
    return (double)ns / 1000000.0;
}

static void init_atomic_microbench_stripes(atomic_uint_fast64_t* values, uint64_t seed) {
    for (int i = 0; i < ATOMIC_MICROBENCH_STRIPES; i++) {
        atomic_init(&values[i], seed + (uint64_t)i);
    }
}

static double benchmark_atomic_load_cost(memory_order order, int iterations) {
    atomic_uint_fast64_t values[ATOMIC_MICROBENCH_STRIPES];
    init_atomic_microbench_stripes(values, 0xC0FFEEu);
    uint64_t start = now_ns();
    uint64_t local_sink = 0;

    for (int i = 0; i < iterations; i++) {
        local_sink += atomic_load_explicit(&values[i & (ATOMIC_MICROBENCH_STRIPES - 1)], order);
    }

    perf_sink ^= local_sink;
    return (double)(now_ns() - start) / (double)iterations;
}

static double benchmark_atomic_store_cost(memory_order order, int iterations) {
    atomic_uint_fast64_t values[ATOMIC_MICROBENCH_STRIPES];
    init_atomic_microbench_stripes(values, 0);
    uint64_t start = now_ns();

    for (int i = 0; i < iterations; i++) {
        atomic_store_explicit(&values[i & (ATOMIC_MICROBENCH_STRIPES - 1)], (uint64_t)i, order);
    }

    for (int i = 0; i < ATOMIC_MICROBENCH_STRIPES; i++) {
        perf_sink ^= atomic_load_explicit(&values[i], memory_order_relaxed);
    }
    return (double)(now_ns() - start) / (double)iterations;
}

static double benchmark_atomic_fetch_add_cost(memory_order order, int iterations) {
    atomic_uint_fast64_t values[ATOMIC_MICROBENCH_STRIPES];
    init_atomic_microbench_stripes(values, 0);
    uint64_t start = now_ns();
    uint64_t local_sink = 0;

    for (int i = 0; i < iterations; i++) {
        local_sink += atomic_fetch_add_explicit(&values[i & (ATOMIC_MICROBENCH_STRIPES - 1)], 1, order);
    }

    perf_sink ^= local_sink;
    return (double)(now_ns() - start) / (double)iterations;
}

static double benchmark_atomic_exchange_cost(memory_order order, int iterations) {
    atomic_uint_fast64_t values[ATOMIC_MICROBENCH_STRIPES];
    init_atomic_microbench_stripes(values, 1);
    uint64_t start = now_ns();
    uint64_t local_sink = 0;

    for (int i = 0; i < iterations; i++) {
        local_sink += atomic_exchange_explicit(
            &values[i & (ATOMIC_MICROBENCH_STRIPES - 1)],
            (uint64_t)((i & 7) + 1),
            order);
    }

    perf_sink ^= local_sink;
    return (double)(now_ns() - start) / (double)iterations;
}

static double benchmark_atomic_cas_success_cost(memory_order success_order,
                                                memory_order failure_order,
                                                int iterations) {
    atomic_uint_fast64_t values[ATOMIC_MICROBENCH_STRIPES];
    init_atomic_microbench_stripes(values, 0);
    uint64_t start = now_ns();
    uint64_t local_sink = 0;

    for (int i = 0; i < iterations; i++) {
        int stripe = i & (ATOMIC_MICROBENCH_STRIPES - 1);
        uint64_t expected = (uint64_t)(stripe + (i / ATOMIC_MICROBENCH_STRIPES));
        bool ok = atomic_compare_exchange_strong_explicit(
            &values[stripe], &expected, expected + 1u, success_order, failure_order);
        if (!ok) {
            local_sink ^= expected;
        }
    }

    for (int i = 0; i < ATOMIC_MICROBENCH_STRIPES; i++) {
        local_sink ^= atomic_load_explicit(&values[i], memory_order_relaxed);
    }
    perf_sink ^= local_sink;
    return (double)(now_ns() - start) / (double)iterations;
}

static double benchmark_atomic_cas_fail_cost(memory_order success_order,
                                             memory_order failure_order,
                                             int iterations) {
    atomic_uint_fast64_t values[ATOMIC_MICROBENCH_STRIPES];
    init_atomic_microbench_stripes(values, 1);
    uint64_t start = now_ns();
    uint64_t local_sink = 0;

    for (int i = 0; i < iterations; i++) {
        uint64_t expected = 0;
        bool ok = atomic_compare_exchange_strong_explicit(
            &values[i & (ATOMIC_MICROBENCH_STRIPES - 1)], &expected, 2, success_order, failure_order);
        local_sink += ok ? 17u : expected;
    }

    perf_sink ^= local_sink;
    return (double)(now_ns() - start) / (double)iterations;
}

static double benchmark_atomic_fence_cost(memory_order order, int iterations) {
    uint64_t start = now_ns();
    uint64_t local_sink = 0;

    for (int i = 0; i < iterations; i++) {
        local_sink += (uint64_t)i;
        atomic_signal_fence(memory_order_seq_cst);
        atomic_thread_fence(order);
        atomic_signal_fence(memory_order_seq_cst);
    }

    perf_sink ^= local_sink;
    return (double)(now_ns() - start) / (double)iterations;
}

static void profile_atomic_operation_costs(void) {
    const char* arch = perf_arch_name();
    const char* lane = perf_atomic_lane_name();
    const int iterations = 1500000 * get_perf_scale();
    double load_relaxed = benchmark_atomic_load_cost(memory_order_relaxed, iterations);
    double store_relaxed = benchmark_atomic_store_cost(memory_order_relaxed, iterations);
    double fetch_add_relaxed = benchmark_atomic_fetch_add_cost(memory_order_relaxed, iterations);
    double cas_success_acq_rel = benchmark_atomic_cas_success_cost(
        memory_order_acq_rel, memory_order_acquire, iterations);
    double cas_fail_acquire = benchmark_atomic_cas_fail_cost(
        memory_order_acq_rel, memory_order_acquire, iterations);

    printf("[atomic_cost_lane] arch: %s, lane: %s, iterations: %d\n",
           arch,
           lane,
           iterations);
    printf("[atomic_cost_common] load_relaxed: %.2f ns, store_relaxed: %.2f ns, fetch_add_relaxed: %.2f ns, cas_success_acq_rel: %.2f ns, cas_fail_acquire: %.2f ns\n",
           load_relaxed,
           store_relaxed,
           fetch_add_relaxed,
           cas_success_acq_rel,
           cas_fail_acquire);

    if (perf_atomic_lane_is_x86()) {
        double load_acquire = benchmark_atomic_load_cost(memory_order_acquire, iterations);
        double store_release = benchmark_atomic_store_cost(memory_order_release, iterations);
        double fetch_add_seq_cst = benchmark_atomic_fetch_add_cost(memory_order_seq_cst, iterations);
        double exchange_seq_cst = benchmark_atomic_exchange_cost(memory_order_seq_cst, iterations);
        double fence_seq_cst = benchmark_atomic_fence_cost(memory_order_seq_cst, iterations);

        printf("[atomic_cost_x86] load_acquire: %.2f ns, store_release: %.2f ns, fetch_add_seq_cst: %.2f ns, exchange_seq_cst: %.2f ns, fence_seq_cst: %.2f ns\n",
               load_acquire,
               store_release,
               fetch_add_seq_cst,
               exchange_seq_cst,
               fence_seq_cst);
        printf("[atomic_cost_x86_ratios] seq_cst_fetch_add/relaxed: %.2fx, seq_cst_fence/relaxed_fetch_add: %.2fx\n",
               fetch_add_relaxed > 0.0 ? fetch_add_seq_cst / fetch_add_relaxed : 0.0,
               fetch_add_relaxed > 0.0 ? fence_seq_cst / fetch_add_relaxed : 0.0);
        if (fence_seq_cst > fetch_add_relaxed * 2.0) {
            printf("  note: x86 TSO keeps basic acquire/release costs close to relaxed, but seq_cst fences still serialize noticeably\n");
        }
        return;
    }

    if (perf_atomic_lane_is_arm()) {
        double load_acquire = benchmark_atomic_load_cost(memory_order_acquire, iterations);
        double store_release = benchmark_atomic_store_cost(memory_order_release, iterations);
        double fetch_add_acq_rel = benchmark_atomic_fetch_add_cost(memory_order_acq_rel, iterations);
        double fence_acq_rel = benchmark_atomic_fence_cost(memory_order_acq_rel, iterations);
        double fence_seq_cst = benchmark_atomic_fence_cost(memory_order_seq_cst, iterations);

        printf("[atomic_cost_arm] load_acquire: %.2f ns, store_release: %.2f ns, fetch_add_acq_rel: %.2f ns, fence_acq_rel: %.2f ns, fence_seq_cst: %.2f ns\n",
               load_acquire,
               store_release,
               fetch_add_acq_rel,
               fence_acq_rel,
               fence_seq_cst);
        printf("[atomic_cost_arm_ratios] acq_rel_fetch_add/relaxed: %.2fx, seq_cst_fence/relaxed_fetch_add: %.2fx\n",
               fetch_add_relaxed > 0.0 ? fetch_add_acq_rel / fetch_add_relaxed : 0.0,
               fetch_add_relaxed > 0.0 ? fence_seq_cst / fetch_add_relaxed : 0.0);
        if (fetch_add_acq_rel > fetch_add_relaxed * 1.10) {
            printf("  note: ARM ordered RMW operations are materially more expensive than relaxed on this host\n");
        }
        return;
    }

    printf("[atomic_cost_generic] compare results only within the same host class; x86 and ARM lanes expose extra ordered-operation probes\n");
}

static Colony make_colony_template(void) {
    Colony colony;
    memset(&colony, 0, sizeof(colony));
    colony.genome = genome_create_random();
    colony.active = true;
    colony.color = colony.genome.body_color;
    return colony;
}

static World* make_world_with_colonies(int width, int height, int colony_count) {
    World* world = world_create(width, height);
    if (!world) {
        return NULL;
    }

    for (int i = 0; i < colony_count; i++) {
        Colony colony = make_colony_template();
        if (world_add_colony(world, colony) == 0) {
            world_destroy(world);
            return NULL;
        }
    }

    size_t cells = (size_t)width * (size_t)height;
    for (size_t i = 0; i < cells; i++) {
        world->cells[i].colony_id = world->colonies[i % (size_t)colony_count].id;
        world->cells[i].age = (uint8_t)(i & 0xFFu);
    }

    for (int c = 0; c < colony_count; c++) {
        world->colonies[c].cell_count = cells / (size_t)colony_count;
        world->colonies[c].max_cell_count = world->colonies[c].cell_count;
    }

    return world;
}

static World* make_spread_benchmark_world(int width, int height, int colony_count) {
    World* world = world_create(width, height);
    if (!world) {
        return NULL;
    }

    for (int i = 0; i < colony_count; i++) {
        Colony colony = make_colony_template();
        colony.genome.spread_rate = 0.40f;
        colony.genome.metabolism = 1.0f;
        colony.genome.social_factor = 0.25f;
        colony.genome.detection_range = 0.30f;
        colony.genome.max_tracked = 8;
        for (int d = 0; d < 8; d++) {
            colony.genome.spread_weights[d] = 1.0f;
        }
        if (world_add_colony(world, colony) == 0) {
            world_destroy(world);
            return NULL;
        }
    }

    size_t* counts = (size_t*)calloc((size_t)colony_count, sizeof(size_t));
    if (!counts) {
        world_destroy(world);
        return NULL;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            Cell* cell = &world->cells[idx];

            if (((x + y) & 1) == 0) {
                int colony_slot = ((x / 2) + (y / 2)) % colony_count;
                cell->colony_id = world->colonies[colony_slot].id;
                cell->age = 32;
                counts[colony_slot]++;
            } else {
                cell->colony_id = 0;
                cell->age = 0;
            }
        }
    }

    for (int c = 0; c < colony_count; c++) {
        world->colonies[c].cell_count = counts[c];
        world->colonies[c].max_cell_count = counts[c];
    }

    free(counts);
    return world;
}

static double benchmark_world_get_colony(World* world, const uint32_t* ids, size_t id_count, int repeats) {
    uint64_t start = now_ns();
    uint64_t local_sink = 0;

    for (int r = 0; r < repeats; r++) {
        for (size_t i = 0; i < id_count; i++) {
            Colony* c = world_get_colony(world, ids[i]);
            if (c) {
                local_sink += c->id;
            }
        }
    }

    perf_sink ^= local_sink;
    uint64_t elapsed = now_ns() - start;
    return (double)elapsed / (double)(id_count * (size_t)repeats);
}

static void profile_world_get_colony_scaling(void) {
    const int width = 64;
    const int height = 64;
    const int repeats = 200;
    const size_t lookups = 4096;

    World* small = make_world_with_colonies(width, height, 32);
    World* large = make_world_with_colonies(width, height, 512);
    if (!small || !large) {
        printf("[ERROR] failed to set up world_get_colony benchmark worlds\n");
        world_destroy(small);
        world_destroy(large);
        return;
    }

    uint32_t* ids_small = (uint32_t*)malloc(sizeof(uint32_t) * lookups);
    uint32_t* ids_large = (uint32_t*)malloc(sizeof(uint32_t) * lookups);
    if (!ids_small || !ids_large) {
        printf("[ERROR] failed to allocate lookup vectors\n");
        free(ids_small);
        free(ids_large);
        world_destroy(small);
        world_destroy(large);
        return;
    }

    for (size_t i = 0; i < lookups; i++) {
        ids_small[i] = small->colonies[i % 32].id;
        ids_large[i] = large->colonies[i % 512].id;
    }

    double ns_small = benchmark_world_get_colony(small, ids_small, lookups, repeats);
    double ns_large = benchmark_world_get_colony(large, ids_large, lookups, repeats);
    double ratio = ns_large / ns_small;

    printf("[world_get_colony] 32 colonies: %.1f ns/lookup, 512 colonies: %.1f ns/lookup, scale ratio: %.2fx\n",
           ns_small, ns_large, ratio);
    if (ratio > 2.0) {
        printf("  hotspot: lookup cost scales strongly with colony count (linear scan likely in hot path)\n");
    }

    free(ids_small);
    free(ids_large);
    world_destroy(small);
    world_destroy(large);
}

static double benchmark_update_colony_stats(World* world, int repeats) {
    uint64_t start = now_ns();
    for (int i = 0; i < repeats; i++) {
        simulation_update_colony_stats(world);
    }
    uint64_t elapsed = now_ns() - start;
    perf_sink ^= (uint64_t)world->tick;
    return ns_to_ms(elapsed);
}

static void profile_simulation_update_colony_stats(void) {
    const int width = 220;
    const int height = 220;
    const int repeats = 20;

    World* low = make_world_with_colonies(width, height, 16);
    World* high = make_world_with_colonies(width, height, 256);
    if (!low || !high) {
        printf("[ERROR] failed to set up simulation_update_colony_stats benchmark worlds\n");
        world_destroy(low);
        world_destroy(high);
        return;
    }

    double low_ms = benchmark_update_colony_stats(low, repeats);
    double high_ms = benchmark_update_colony_stats(high, repeats);
    double ratio = high_ms / low_ms;

    printf("[simulation_update_colony_stats] 16 colonies: %.2f ms/%d runs, 256 colonies: %.2f ms/%d runs, scale ratio: %.2fx\n",
           low_ms, repeats, high_ms, repeats, ratio);
    if (ratio > 2.0) {
        printf("  hotspot: per-cell colony accounting appears to scale with colony count\n");
    }

    world_destroy(low);
    world_destroy(high);
}

static void fill_proto_world_grid(ProtoWorld* world, bool noisy) {
    uint32_t size = world->width * world->height;
    for (uint32_t i = 0; i < size; i++) {
        if (noisy) {
            world->grid[i] = (uint16_t)((i * 131u) & 0x00FFu);
        } else {
            world->grid[i] = (i % 80u == 0u) ? 5u : 0u;
        }
    }
}

static void setup_proto_world(ProtoWorld* world, bool noisy_grid) {
    proto_world_init(world);
    world->width = 800;
    world->height = 400;
    world->tick = 1234;
    world->paused = false;
    world->speed_multiplier = 1.0f;
    world->colony_count = 64;

    for (uint32_t i = 0; i < world->colony_count; i++) {
        ProtoColony* c = &world->colonies[i];
        memset(c, 0, sizeof(*c));
        c->id = i + 1;
        c->x = (float)(i * 11);
        c->y = (float)(i * 7);
        c->radius = 4.0f + (float)(i % 9);
        c->population = 100u + i;
        c->max_population = 120u + i;
        c->growth_rate = 0.4f;
        c->color_r = (uint8_t)(50 + (i * 3) % 200);
        c->color_g = (uint8_t)(80 + (i * 5) % 160);
        c->color_b = (uint8_t)(100 + (i * 7) % 120);
        c->alive = true;
        c->shape_seed = i * 17u;
        c->wobble_phase = 0.1f * (float)i;
        c->shape_evolution = 0.02f * (float)(i % 10);
    }

    proto_world_alloc_grid(world, world->width, world->height);
    fill_proto_world_grid(world, noisy_grid);
}

static void profile_protocol_world_state_cost(void) {
    ProtoWorld sparse;
    ProtoWorld noisy;
    ProtoWorld decoded;
    const int repeats = 10;

    setup_proto_world(&sparse, false);
    setup_proto_world(&noisy, true);

    uint64_t start_sparse = now_ns();
    size_t sparse_bytes_total = 0;
    for (int i = 0; i < repeats; i++) {
        uint8_t* buffer = NULL;
        size_t len = 0;
        if (protocol_serialize_world_state(&sparse, &buffer, &len) == 0) {
            sparse_bytes_total += len;
            proto_world_init(&decoded);
            if (protocol_deserialize_world_state(buffer, len, &decoded) == 0) {
                perf_sink ^= decoded.width;
            }
            proto_world_free(&decoded);
            free(buffer);
        }
    }
    uint64_t sparse_elapsed = now_ns() - start_sparse;

    uint64_t start_noisy = now_ns();
    size_t noisy_bytes_total = 0;
    for (int i = 0; i < repeats; i++) {
        uint8_t* buffer = NULL;
        size_t len = 0;
        if (protocol_serialize_world_state(&noisy, &buffer, &len) == 0) {
            noisy_bytes_total += len;
            proto_world_init(&decoded);
            if (protocol_deserialize_world_state(buffer, len, &decoded) == 0) {
                perf_sink ^= decoded.height;
            }
            proto_world_free(&decoded);
            free(buffer);
        }
    }
    uint64_t noisy_elapsed = now_ns() - start_noisy;

    double raw_grid_bytes = (double)(sparse.grid_size * sizeof(uint16_t));
    double sparse_avg = (double)sparse_bytes_total / (double)repeats;
    double noisy_avg = (double)noisy_bytes_total / (double)repeats;
    double sparse_comp = sparse_avg / raw_grid_bytes;
    double noisy_comp = noisy_avg / raw_grid_bytes;

    printf("[protocol_serialize_world_state] sparse avg size: %.0f bytes (%.2fx raw), noisy avg size: %.0f bytes (%.2fx raw)\n",
           sparse_avg, sparse_comp, noisy_avg, noisy_comp);
    printf("[protocol_serialize_world_state] sparse ser+de: %.2f ms/%d runs, noisy ser+de: %.2f ms/%d runs\n",
           ns_to_ms(sparse_elapsed), repeats, ns_to_ms(noisy_elapsed), repeats);
    if (noisy_comp > 1.7) {
        printf("  hotspot: RLE expansion for noisy worlds inflates payload and encode/decode cost\n");
    }

    proto_world_free(&sparse);
    proto_world_free(&noisy);
}

static double benchmark_build_protocol_snapshot(World* world, int repeats, bool* has_grid_out) {
    uint64_t start = now_ns();
    bool saw_grid = false;

    for (int i = 0; i < repeats; i++) {
        ProtoWorld proto_world;
        if (server_build_protocol_world_snapshot(world, false, 1.0f, &proto_world) != 0) {
            return -1.0;
        }
        saw_grid = proto_world.has_grid;
        perf_sink ^= proto_world.colony_count;
        proto_world_free(&proto_world);
    }

    if (has_grid_out) {
        *has_grid_out = saw_grid;
    }

    return ns_to_ms(now_ns() - start);
}

static void profile_server_snapshot_build_cost(void) {
    const int repeats = 8;

    World* inline_world = make_world_with_colonies(400, 200, 50);
    World* chunked_world = make_world_with_colonies(900, 600, 128);
    if (!inline_world || !chunked_world) {
        printf("[ERROR] failed to set up server snapshot benchmark worlds\n");
        world_destroy(inline_world);
        world_destroy(chunked_world);
        return;
    }

    bool inline_has_grid = false;
    bool chunked_has_grid = false;
    double inline_ms = benchmark_build_protocol_snapshot(inline_world, repeats, &inline_has_grid);
    double chunked_ms = benchmark_build_protocol_snapshot(chunked_world, repeats, &chunked_has_grid);
    if (inline_ms < 0.0 || chunked_ms < 0.0) {
        printf("[ERROR] server snapshot benchmark failed\n");
        world_destroy(inline_world);
        world_destroy(chunked_world);
        return;
    }

    printf("[server_build_protocol_world_snapshot] 400x200 inline=%s: %.2f ms/%d runs, 900x600 inline=%s: %.2f ms/%d runs\n",
           inline_has_grid ? "yes" : "no",
           inline_ms,
           repeats,
           chunked_has_grid ? "yes" : "no",
           chunked_ms,
           repeats);

    world_destroy(inline_world);
    world_destroy(chunked_world);
}

static atomic_uint_fast64_t tp_stripes[64];

static void tiny_task(void* arg) {
    atomic_uint_fast64_t* slot = (atomic_uint_fast64_t*)arg;
    atomic_fetch_add_explicit(slot, 1, memory_order_relaxed);
}

static double benchmark_threadpool_tiny_tasks(int workers, int task_count) {
    ThreadPool* pool = threadpool_create(workers);
    if (!pool) {
        return -1.0;
    }

    for (int i = 0; i < 64; i++) {
        atomic_store_explicit(&tp_stripes[i], 0, memory_order_relaxed);
    }

    uint64_t start = now_ns();
    void* args[64];

    int submitted = 0;
    while (submitted < task_count) {
        int emit = task_count - submitted;
        if (emit > 64) {
            emit = 64;
        }

        for (int i = 0; i < emit; i++) {
            args[i] = &tp_stripes[(submitted + i) & 63];
        }

        threadpool_submit_batch(pool, tiny_task, args, emit);
        submitted += emit;
    }
    threadpool_wait(pool);
    uint64_t elapsed = now_ns() - start;

    uint64_t completed = 0;
    for (int i = 0; i < 64; i++) {
        completed += atomic_load_explicit(&tp_stripes[i], memory_order_relaxed);
    }
    perf_sink ^= (uint64_t)completed;
    threadpool_destroy(pool);

    if (completed != (uint64_t)task_count) {
        return -1.0;
    }

    return (double)task_count / ((double)elapsed / 1000000000.0);
}

static void profile_threadpool_tiny_task_throughput(void) {
    int cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) cpus = 1;
    int workers = cpus > 8 ? 8 : cpus;
    const int task_count = 200000;

    double tps1 = benchmark_threadpool_tiny_tasks(1, task_count);
    double tpsn = benchmark_threadpool_tiny_tasks(workers, task_count);

    if (tps1 < 0 || tpsn < 0) {
        printf("[ERROR] threadpool throughput benchmark failed\n");
        return;
    }

    printf("[threadpool tiny tasks] 1 worker: %.0f tasks/s, %d workers: %.0f tasks/s, speedup: %.2fx\n",
           tps1, workers, tpsn, tpsn / tps1);
    if (tpsn < tps1 * 1.2) {
        printf("  hotspot: tiny task overhead/queue contention limits multicore scaling\n");
    }
}

static World* make_fragmented_world(int width, int height) {
    World* world = world_create(width, height);
    if (!world) {
        return NULL;
    }

    Colony colony = make_colony_template();
    uint32_t id = world_add_colony(world, colony);
    if (id == 0) {
        world_destroy(world);
        return NULL;
    }

    size_t count = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (((x + y) & 1) == 0) {
                world->cells[y * width + x].colony_id = id;
                count++;
            }
        }
    }

    Colony* col = world_get_colony(world, id);
    if (col) {
        col->cell_count = count;
        col->max_cell_count = count;
    }

    return world;
}

static void profile_division_fragmentation_scaling(void) {
    World* small = make_fragmented_world(120, 120);
    World* large = make_fragmented_world(240, 240);
    if (!small || !large) {
        printf("[ERROR] failed to set up division-scaling benchmark worlds\n");
        world_destroy(small);
        world_destroy(large);
        return;
    }

    uint64_t start_small = now_ns();
    simulation_check_divisions(small);
    uint64_t small_ns = now_ns() - start_small;

    uint64_t start_large = now_ns();
    simulation_check_divisions(large);
    uint64_t large_ns = now_ns() - start_large;

    double ratio = (double)large_ns / (double)small_ns;
    printf("[simulation_check_divisions] 120x120: %.2f ms, 240x240: %.2f ms, scale ratio: %.2fx\n",
           ns_to_ms(small_ns), ns_to_ms(large_ns), ratio);
    if (ratio > 6.0) {
        printf("  hotspot: division handling scales worse than area growth under fragmentation\n");
    }

    world_destroy(small);
    world_destroy(large);
}

static void profile_atomic_tick_scaling(void) {
    const int width = 220;
    const int height = 220;
    const int colony_count = 128;
    const int ticks = 12;

    World* world1 = make_world_with_colonies(width, height, colony_count);
    World* worldn = make_world_with_colonies(width, height, colony_count);
    if (!world1 || !worldn) {
        printf("[ERROR] failed to set up atomic-tick benchmark worlds\n");
        world_destroy(world1);
        world_destroy(worldn);
        return;
    }

    int cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) cpus = 1;
    int workers = cpus > 8 ? 8 : cpus;

    ThreadPool* pool1 = threadpool_create(1);
    ThreadPool* pooln = threadpool_create(workers);
    AtomicWorld* aw1 = atomic_world_create(world1, pool1, 1);
    AtomicWorld* awn = atomic_world_create(worldn, pooln, workers);

    if (!pool1 || !pooln || !aw1 || !awn) {
        printf("[ERROR] failed to set up atomic worlds/threadpools\n");
        atomic_world_destroy(aw1);
        atomic_world_destroy(awn);
        threadpool_destroy(pool1);
        threadpool_destroy(pooln);
        world_destroy(world1);
        world_destroy(worldn);
        return;
    }

    uint64_t start1 = now_ns();
    for (int i = 0; i < ticks; i++) {
        atomic_tick(aw1);
    }
    uint64_t t1 = now_ns() - start1;

    uint64_t startn = now_ns();
    for (int i = 0; i < ticks; i++) {
        atomic_tick(awn);
    }
    uint64_t tn = now_ns() - startn;

    printf("[atomic_tick] 1 worker: %.2f ms/%d ticks, %d workers: %.2f ms/%d ticks, speedup: %.2fx\n",
           ns_to_ms(t1), ticks, workers, ns_to_ms(tn), ticks, (double)t1 / (double)tn);
    if (tn > (uint64_t)(t1 * 0.9)) {
        printf("  hotspot: atomic path shows weak multicore scaling (serial phases and sync likely dominate)\n");
    }

    atomic_world_destroy(aw1);
    atomic_world_destroy(awn);
    threadpool_destroy(pool1);
    threadpool_destroy(pooln);
    world_destroy(world1);
    world_destroy(worldn);
}

typedef struct {
    uint64_t spread_ns;
    uint64_t apply_ns;
    int64_t claims;
    double frontier_avg;
} AtomicSpreadBenchmark;

static AtomicSpreadBenchmark run_atomic_spread_benchmark(AtomicWorld* aworld, int steps) {
    AtomicSpreadBenchmark out;
    memset(&out, 0, sizeof(out));

    for (int i = 0; i < steps; i++) {
        out.frontier_avg += (double)atomic_get_spread_frontier_count(aworld);

        uint64_t spread_start = now_ns();
        atomic_spread(aworld);
        atomic_barrier(aworld);
        out.spread_ns += now_ns() - spread_start;

        uint64_t apply_start = now_ns();
        out.claims += atomic_spread_apply_deltas(aworld);
        out.apply_ns += now_ns() - apply_start;
    }

    if (steps > 0) {
        out.frontier_avg /= (double)steps;
    }

    return out;
}

static void profile_atomic_spread_phase_scaling(void) {
    const int width = 220;
    const int height = 220;
    const int colony_count = 128;
    const int steps = 40;

    World* world1 = make_spread_benchmark_world(width, height, colony_count);
    World* worldn = make_spread_benchmark_world(width, height, colony_count);
    if (!world1 || !worldn) {
        printf("[ERROR] failed to set up atomic-spread benchmark worlds\n");
        world_destroy(world1);
        world_destroy(worldn);
        return;
    }

    int cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) cpus = 1;
    int workers = cpus > 8 ? 8 : cpus;

    ThreadPool* pool1 = threadpool_create(1);
    ThreadPool* pooln = threadpool_create(workers);
    AtomicWorld* aw1 = atomic_world_create(world1, pool1, 1);
    AtomicWorld* awn = atomic_world_create(worldn, pooln, workers);
    if (!pool1 || !pooln || !aw1 || !awn) {
        printf("[ERROR] failed to set up atomic spread benchmark worlds/threadpools\n");
        atomic_world_destroy(aw1);
        atomic_world_destroy(awn);
        threadpool_destroy(pool1);
        threadpool_destroy(pooln);
        world_destroy(world1);
        world_destroy(worldn);
        return;
    }

    atomic_set_spread_frontier_enabled(aw1, false);
    atomic_set_spread_frontier_enabled(awn, false);
    AtomicSpreadBenchmark baseline1 = run_atomic_spread_benchmark(aw1, steps);
    AtomicSpreadBenchmark baselinen = run_atomic_spread_benchmark(awn, steps);

    atomic_world_sync_from_world(aw1);
    atomic_world_sync_from_world(awn);

    atomic_set_spread_frontier_enabled(aw1, true);
    atomic_set_spread_frontier_enabled(awn, true);
    AtomicSpreadBenchmark frontier1 = run_atomic_spread_benchmark(aw1, steps);
    AtomicSpreadBenchmark frontiern = run_atomic_spread_benchmark(awn, steps);

    uint64_t t1 = frontier1.spread_ns + frontier1.apply_ns;
    uint64_t tn = frontiern.spread_ns + frontiern.apply_ns;

    double spread_speedup = (double)frontier1.spread_ns / (double)frontiern.spread_ns;
    double total_speedup = (double)t1 / (double)tn;
    double apply_share_1 = (double)frontier1.apply_ns / (double)t1;
    double apply_share_n = (double)frontiern.apply_ns / (double)tn;
    double claims_per_sec_1 = frontier1.claims > 0 ? (double)frontier1.claims / ((double)t1 / 1000000000.0) : 0.0;
    double claims_per_sec_n = frontiern.claims > 0 ? (double)frontiern.claims / ((double)tn / 1000000000.0) : 0.0;

    uint64_t baseline1_total = baseline1.spread_ns + baseline1.apply_ns;
    uint64_t baselinen_total = baselinen.spread_ns + baselinen.apply_ns;
    double frontier_gain_1 = (double)baseline1_total / (double)t1;
    double frontier_gain_n = (double)baselinen_total / (double)tn;

    printf("[atomic_spread_frontier_compare] avg frontier cells: %.0f (1w) / %.0f (%dw), baseline: %.2f ms / %.2f ms, frontier: %.2f ms / %.2f ms, gain: %.2fx / %.2fx\n",
           frontier1.frontier_avg,
           frontiern.frontier_avg,
           workers,
           ns_to_ms(baseline1_total),
           ns_to_ms(baselinen_total),
           ns_to_ms(t1),
           ns_to_ms(tn),
           frontier_gain_1,
           frontier_gain_n);
    if (frontier_gain_n < 1.05) {
        printf("  hotspot: frontier scheduling gives limited gain on this density profile\n");
    }

    printf("[atomic_spread_step] phase workers enabled: %d, 1 worker: %.2f ms/%d steps, %d workers: %.2f ms/%d steps, speedup: %.2fx\n",
           awn->phase_system_ready ? 1 : 0,
           ns_to_ms(t1), steps,
           workers,
           ns_to_ms(tn), steps,
           total_speedup);
    printf("[atomic_spread_breakdown_time] spread: %.2f ms (1 worker) / %.2f ms (%d workers), apply: %.3f ms / %.3f ms\n",
           ns_to_ms(frontier1.spread_ns),
           ns_to_ms(frontiern.spread_ns),
           workers,
           ns_to_ms(frontier1.apply_ns),
           ns_to_ms(frontiern.apply_ns));
    printf("[atomic_spread_breakdown] spread kernel speedup: %.2fx, apply share: %.2f%% (1 worker) / %.2f%% (%d workers)\n",
           spread_speedup,
           apply_share_1 * 100.0,
           apply_share_n * 100.0,
           workers);
    printf("[atomic_spread_claims] 1 worker: %lld claims (%.0f claims/s), %d workers: %lld claims (%.0f claims/s)\n",
           (long long)frontier1.claims,
           claims_per_sec_1,
           workers,
           (long long)frontiern.claims,
           claims_per_sec_n);
    if (spread_speedup < 1.2) {
        printf("  hotspot: spread kernel still scales weakly after isolating apply phase\n");
    }

    atomic_world_destroy(aw1);
    atomic_world_destroy(awn);
    threadpool_destroy(pool1);
    threadpool_destroy(pooln);
    world_destroy(world1);
    world_destroy(worldn);
}

int main(void) {
    rng_seed(42);

    printf("\n=== Ferox Performance Profiling Tests ===\n\n");
    printf("    (Set FEROX_PERF_SCALE=2..10 for heavier atomic microbench loops)\n\n");

    profile_platform_characteristics();
    profile_atomic_operation_costs();
    profile_world_get_colony_scaling();
    profile_simulation_update_colony_stats();
    profile_protocol_world_state_cost();
    profile_server_snapshot_build_cost();
    profile_threadpool_tiny_task_throughput();
    profile_division_fragmentation_scaling();
    profile_atomic_spread_phase_scaling();
    profile_atomic_tick_scaling();

    printf("\nPerformance profiling completed. sink=%llu\n", (unsigned long long)perf_sink);
    return 0;
}
