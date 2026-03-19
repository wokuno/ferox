/**
 * test_parallel_edge.c - Branch-focused tests for parallel orchestration
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "../src/server/threadpool.h"
#include "../src/server/parallel.h"
#include "../src/server/simulation.h"
#include "../src/server/world.h"

#define TEST_START(name) printf("  Testing %s... ", name)
#define TEST_PASS() printf("PASSED\n")
#define TEST_FAIL(msg) do { printf("FAILED: %s\n", msg); return 1; } while (0)

#define ASSERT_TRUE(cond) do { if (!(cond)) TEST_FAIL(#cond " is false"); } while (0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) TEST_FAIL(#a " != " #b); } while (0)
#define ASSERT_NOT_NULL(ptr) do { if ((ptr) == NULL) TEST_FAIL(#ptr " is NULL"); } while (0)

static void increment_task(void* arg) {
    int* counter = (int*)arg;
    (*counter)++;
}

static int test_threadpool_submit_handles_null_inputs(void) {
    TEST_START("threadpool_submit null inputs");

    int counter = 0;
    threadpool_submit(NULL, increment_task, &counter);

    ThreadPool* pool = threadpool_create(1);
    ASSERT_NOT_NULL(pool);

    threadpool_submit(pool, NULL, &counter);
    threadpool_wait(pool);
    ASSERT_EQ(counter, 0);

    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_threadpool_submit_ignored_during_shutdown(void) {
    TEST_START("threadpool_submit shutdown branch");

    ThreadPool* pool = threadpool_create(1);
    ASSERT_NOT_NULL(pool);

    int counter = 0;

    pthread_mutex_lock(&pool->queue_mutex);
    pool->counters.shutdown = true;
    pthread_mutex_unlock(&pool->queue_mutex);

    threadpool_submit(pool, increment_task, &counter);
    threadpool_wait(pool);
    ASSERT_EQ(counter, 0);

    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_parallel_init_regions_ignores_invalid_dimensions(void) {
    TEST_START("parallel_init_regions invalid dimensions");

    ThreadPool* pool = threadpool_create(1);
    ASSERT_NOT_NULL(pool);
    ParallelContext* ctx = parallel_create(pool, NULL, 2, 2);
    ASSERT_NOT_NULL(ctx);

    parallel_init_regions(ctx, 0, 10);
    ASSERT_EQ(ctx->regions[0].end_x, 0);
    ASSERT_EQ(ctx->regions[0].end_y, 0);

    parallel_init_regions(ctx, 10, -1);
    ASSERT_EQ(ctx->regions[1].end_x, 0);
    ASSERT_EQ(ctx->regions[1].end_y, 0);

    parallel_destroy(ctx);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_parallel_api_handles_null_context(void) {
    TEST_START("parallel APIs handle NULL context");

    parallel_age(NULL);
    parallel_spread(NULL);
    parallel_mutate(NULL);
    parallel_barrier(NULL);
    parallel_init_regions(NULL, 10, 10);
    parallel_tick(NULL);
    threadpool_wait(NULL);
    threadpool_destroy(NULL);

    TEST_PASS();
    return 0;
}

static int test_parallel_tick_returns_when_world_is_null(void) {
    TEST_START("parallel_tick world NULL");

    ThreadPool* pool = threadpool_create(2);
    ASSERT_NOT_NULL(pool);
    ParallelContext* ctx = parallel_create(pool, NULL, 1, 1);
    ASSERT_NOT_NULL(ctx);

    parallel_tick(ctx);

    ASSERT_EQ(pool->counters.pending_tasks, 0);
    ASSERT_EQ(pool->counters.active_tasks, 0);

    parallel_destroy(ctx);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_parallel_spread_skips_null_pending_buffer(void) {
    TEST_START("parallel_spread skips NULL pending buffer");

    ThreadPool* pool = threadpool_create(2);
    ASSERT_NOT_NULL(pool);
    World* world = world_create(4, 4);
    ASSERT_NOT_NULL(world);
    ParallelContext* ctx = parallel_create(pool, world, 1, 1);
    ASSERT_NOT_NULL(ctx);

    parallel_init_regions(ctx, world->width, world->height);

    pending_buffer_destroy(ctx->pending_buffers[0]);
    ctx->pending_buffers[0] = NULL;

    parallel_spread(ctx);
    parallel_barrier(ctx);
    ASSERT_TRUE(ctx->pending_buffers[0] == NULL);

    parallel_destroy(ctx);
    world_destroy(world);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

int main(void) {
    int failed = 0;

    printf("\n=== Parallel Edge Branch Tests ===\n\n");

    failed += test_threadpool_submit_handles_null_inputs();
    failed += test_threadpool_submit_ignored_during_shutdown();
    failed += test_parallel_init_regions_ignores_invalid_dimensions();
    failed += test_parallel_api_handles_null_context();
    failed += test_parallel_tick_returns_when_world_is_null();
    failed += test_parallel_spread_skips_null_pending_buffer();

    printf("\n=== Results ===\n");
    if (failed == 0) {
        printf("All tests PASSED!\n");
        return 0;
    }

    printf("%d test(s) FAILED!\n", failed);
    return 1;
}
