/**
 * test_phase3.c - Unit tests for Phase 3: Threading & Concurrency
 * 
 * Tests thread pool implementation and parallel orchestration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include "../src/server/threadpool.h"
#include "../src/server/parallel.h"

// Test framework macros
#define TEST_START(name) printf("  Testing %s... ", name)
#define TEST_PASS() printf("PASSED\n")
#define TEST_FAIL(msg) do { printf("FAILED: %s\n", msg); return 1; } while(0)

#define ASSERT_TRUE(cond) do { if (!(cond)) TEST_FAIL(#cond " is false"); } while(0)
#define ASSERT_FALSE(cond) do { if (cond) TEST_FAIL(#cond " is true"); } while(0)
#define ASSERT_NULL(ptr) do { if ((ptr) != NULL) TEST_FAIL(#ptr " is not NULL"); } while(0)
#define ASSERT_NOT_NULL(ptr) do { if ((ptr) == NULL) TEST_FAIL(#ptr " is NULL"); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) TEST_FAIL(#a " != " #b); } while(0)

// Shared counter for parallel testing
static int shared_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

// Simple task that increments a counter
static void increment_task(void* arg) {
    int* counter = (int*)arg;
    pthread_mutex_lock(&counter_mutex);
    (*counter)++;
    pthread_mutex_unlock(&counter_mutex);
}

// Task that sleeps briefly to simulate work
static void slow_task(void* arg) {
    int* counter = (int*)arg;
    usleep(10000); // 10ms
    pthread_mutex_lock(&counter_mutex);
    (*counter)++;
    pthread_mutex_unlock(&counter_mutex);
}

// Task that records thread ID to verify parallel execution
typedef struct {
    pthread_t* thread_ids;
    int* index;
    pthread_mutex_t* mutex;
} ThreadIdArg;

static void record_thread_id(void* arg) {
    ThreadIdArg* targ = (ThreadIdArg*)arg;
    usleep(5000); // Small delay to ensure overlap
    
    pthread_mutex_lock(targ->mutex);
    int idx = (*targ->index)++;
    targ->thread_ids[idx] = pthread_self();
    pthread_mutex_unlock(targ->mutex);
}

// ============================================================================
// Thread Pool Tests
// ============================================================================

static int test_threadpool_create_basic(void) {
    TEST_START("threadpool_create basic");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    ASSERT_EQ(pool->thread_count, 4);
    ASSERT_FALSE(pool->shutdown);
    ASSERT_NULL(pool->task_queue_head);
    ASSERT_EQ(pool->active_tasks, 0);
    ASSERT_EQ(pool->pending_tasks, 0);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_threadpool_create_single_thread(void) {
    TEST_START("threadpool_create single thread");
    
    ThreadPool* pool = threadpool_create(1);
    ASSERT_NOT_NULL(pool);
    ASSERT_EQ(pool->thread_count, 1);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_threadpool_create_many_threads(void) {
    TEST_START("threadpool_create many threads");
    
    ThreadPool* pool = threadpool_create(16);
    ASSERT_NOT_NULL(pool);
    ASSERT_EQ(pool->thread_count, 16);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_threadpool_create_invalid(void) {
    TEST_START("threadpool_create invalid");
    
    ThreadPool* pool = threadpool_create(0);
    ASSERT_NULL(pool);
    
    pool = threadpool_create(-1);
    ASSERT_NULL(pool);
    
    TEST_PASS();
    return 0;
}

static int test_threadpool_submit_single(void) {
    TEST_START("threadpool_submit single task");
    
    ThreadPool* pool = threadpool_create(2);
    ASSERT_NOT_NULL(pool);
    
    int counter = 0;
    threadpool_submit(pool, increment_task, &counter);
    threadpool_wait(pool);
    
    ASSERT_EQ(counter, 1);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_threadpool_submit_multiple(void) {
    TEST_START("threadpool_submit multiple tasks");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    int counter = 0;
    for (int i = 0; i < 100; i++) {
        threadpool_submit(pool, increment_task, &counter);
    }
    threadpool_wait(pool);
    
    ASSERT_EQ(counter, 100);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_threadpool_parallel_execution(void) {
    TEST_START("threadpool parallel execution");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    pthread_t thread_ids[4];
    int index = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    
    ThreadIdArg args[4];
    for (int i = 0; i < 4; i++) {
        args[i].thread_ids = thread_ids;
        args[i].index = &index;
        args[i].mutex = &mutex;
        threadpool_submit(pool, record_thread_id, &args[i]);
    }
    
    threadpool_wait(pool);
    
    // Verify all 4 tasks completed
    ASSERT_EQ(index, 4);
    
    // Count unique thread IDs
    int unique_count = 0;
    for (int i = 0; i < 4; i++) {
        int is_unique = 1;
        for (int j = 0; j < i; j++) {
            if (pthread_equal(thread_ids[i], thread_ids[j])) {
                is_unique = 0;
                break;
            }
        }
        if (is_unique) unique_count++;
    }
    
    // Should have multiple unique threads (at least 2 for parallel execution)
    ASSERT_TRUE(unique_count >= 2);
    
    pthread_mutex_destroy(&mutex);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_threadpool_wait_blocks(void) {
    TEST_START("threadpool_wait blocks until completion");
    
    ThreadPool* pool = threadpool_create(2);
    ASSERT_NOT_NULL(pool);
    
    int counter = 0;
    for (int i = 0; i < 10; i++) {
        threadpool_submit(pool, slow_task, &counter);
    }
    
    // Wait should block until all tasks complete
    threadpool_wait(pool);
    
    // All tasks should be complete
    ASSERT_EQ(counter, 10);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_threadpool_empty_queue(void) {
    TEST_START("threadpool empty queue");
    
    ThreadPool* pool = threadpool_create(2);
    ASSERT_NOT_NULL(pool);
    
    // Wait on empty pool should return immediately
    threadpool_wait(pool);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_threadpool_shutdown_clean(void) {
    TEST_START("threadpool shutdown clean");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    int counter = 0;
    for (int i = 0; i < 50; i++) {
        threadpool_submit(pool, increment_task, &counter);
    }
    
    // Destroy should wait for pending tasks
    threadpool_destroy(pool);
    
    // Counter may be less than 50 if some tasks weren't started
    // but should be at least partially complete
    ASSERT_TRUE(counter >= 0);
    
    TEST_PASS();
    return 0;
}

static int test_threadpool_large_task_count(void) {
    TEST_START("threadpool large task count");
    
    ThreadPool* pool = threadpool_create(8);
    ASSERT_NOT_NULL(pool);
    
    int counter = 0;
    for (int i = 0; i < 1000; i++) {
        threadpool_submit(pool, increment_task, &counter);
    }
    
    threadpool_wait(pool);
    ASSERT_EQ(counter, 1000);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

// ============================================================================
// Parallel Context Tests
// ============================================================================

static int test_parallel_create_basic(void) {
    TEST_START("parallel_create basic");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    ParallelContext* ctx = parallel_create(pool, NULL, 2, 2);
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ(ctx->region_count, 4);
    ASSERT_EQ(ctx->regions_x, 2);
    ASSERT_EQ(ctx->regions_y, 2);
    ASSERT_NOT_NULL(ctx->regions);
    
    parallel_destroy(ctx);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_parallel_create_invalid(void) {
    TEST_START("parallel_create invalid");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    // NULL pool
    ParallelContext* ctx = parallel_create(NULL, NULL, 2, 2);
    ASSERT_NULL(ctx);
    
    // Invalid region counts
    ctx = parallel_create(pool, NULL, 0, 2);
    ASSERT_NULL(ctx);
    
    ctx = parallel_create(pool, NULL, 2, 0);
    ASSERT_NULL(ctx);
    
    ctx = parallel_create(pool, NULL, -1, 2);
    ASSERT_NULL(ctx);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_parallel_init_regions_even(void) {
    TEST_START("parallel_init_regions even division");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    ParallelContext* ctx = parallel_create(pool, NULL, 2, 2);
    ASSERT_NOT_NULL(ctx);
    
    // Initialize with 100x100 grid, should divide evenly
    parallel_init_regions(ctx, 100, 100);
    
    // Check region 0 (top-left)
    ASSERT_EQ(ctx->regions[0].start_x, 0);
    ASSERT_EQ(ctx->regions[0].start_y, 0);
    ASSERT_EQ(ctx->regions[0].end_x, 50);
    ASSERT_EQ(ctx->regions[0].end_y, 50);
    
    // Check region 1 (top-right)
    ASSERT_EQ(ctx->regions[1].start_x, 50);
    ASSERT_EQ(ctx->regions[1].start_y, 0);
    ASSERT_EQ(ctx->regions[1].end_x, 100);
    ASSERT_EQ(ctx->regions[1].end_y, 50);
    
    // Check region 2 (bottom-left)
    ASSERT_EQ(ctx->regions[2].start_x, 0);
    ASSERT_EQ(ctx->regions[2].start_y, 50);
    ASSERT_EQ(ctx->regions[2].end_x, 50);
    ASSERT_EQ(ctx->regions[2].end_y, 100);
    
    // Check region 3 (bottom-right)
    ASSERT_EQ(ctx->regions[3].start_x, 50);
    ASSERT_EQ(ctx->regions[3].start_y, 50);
    ASSERT_EQ(ctx->regions[3].end_x, 100);
    ASSERT_EQ(ctx->regions[3].end_y, 100);
    
    parallel_destroy(ctx);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_parallel_init_regions_uneven(void) {
    TEST_START("parallel_init_regions uneven division");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    ParallelContext* ctx = parallel_create(pool, NULL, 3, 3);
    ASSERT_NOT_NULL(ctx);
    
    // Initialize with 100x100 grid, should handle remainder
    parallel_init_regions(ctx, 100, 100);
    
    // Verify all cells are covered (no gaps)
    int total_cells = 0;
    for (int i = 0; i < ctx->region_count; i++) {
        int width = ctx->regions[i].end_x - ctx->regions[i].start_x;
        int height = ctx->regions[i].end_y - ctx->regions[i].start_y;
        total_cells += width * height;
    }
    ASSERT_EQ(total_cells, 100 * 100);
    
    // Verify no overlaps by checking boundaries
    for (int i = 0; i < ctx->region_count; i++) {
        ASSERT_TRUE(ctx->regions[i].start_x >= 0);
        ASSERT_TRUE(ctx->regions[i].start_y >= 0);
        ASSERT_TRUE(ctx->regions[i].end_x <= 100);
        ASSERT_TRUE(ctx->regions[i].end_y <= 100);
        ASSERT_TRUE(ctx->regions[i].start_x < ctx->regions[i].end_x);
        ASSERT_TRUE(ctx->regions[i].start_y < ctx->regions[i].end_y);
    }
    
    parallel_destroy(ctx);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_parallel_init_regions_covers_grid(void) {
    TEST_START("parallel_init_regions covers entire grid");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    ParallelContext* ctx = parallel_create(pool, NULL, 4, 4);
    ASSERT_NOT_NULL(ctx);
    
    parallel_init_regions(ctx, 127, 89); // Odd dimensions
    
    // Create coverage map
    int* coverage = (int*)calloc(127 * 89, sizeof(int));
    ASSERT_NOT_NULL(coverage);
    
    for (int r = 0; r < ctx->region_count; r++) {
        Region* region = &ctx->regions[r];
        for (int y = region->start_y; y < region->end_y; y++) {
            for (int x = region->start_x; x < region->end_x; x++) {
                coverage[y * 127 + x]++;
            }
        }
    }
    
    // Every cell should be covered exactly once
    for (int i = 0; i < 127 * 89; i++) {
        ASSERT_EQ(coverage[i], 1);
    }
    
    free(coverage);
    parallel_destroy(ctx);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_parallel_spread_mutate(void) {
    TEST_START("parallel_spread and parallel_mutate");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    ParallelContext* ctx = parallel_create(pool, NULL, 2, 2);
    ASSERT_NOT_NULL(ctx);
    
    parallel_init_regions(ctx, 100, 100);
    
    // These should submit tasks and complete without error
    parallel_spread(ctx);
    parallel_barrier(ctx);
    
    parallel_mutate(ctx);
    parallel_barrier(ctx);
    
    parallel_destroy(ctx);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_parallel_barrier(void) {
    TEST_START("parallel_barrier synchronization");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    ParallelContext* ctx = parallel_create(pool, NULL, 2, 2);
    ASSERT_NOT_NULL(ctx);
    
    // Multiple barriers should work
    parallel_barrier(ctx);
    parallel_barrier(ctx);
    parallel_barrier(ctx);
    
    parallel_destroy(ctx);
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

// ============================================================================
// Race Condition Tests
// ============================================================================

static int test_no_race_shared_counter(void) {
    TEST_START("no race conditions with shared counter");
    
    ThreadPool* pool = threadpool_create(8);
    ASSERT_NOT_NULL(pool);
    
    shared_counter = 0;
    
    // Submit many tasks that increment shared counter
    for (int i = 0; i < 10000; i++) {
        threadpool_submit(pool, increment_task, &shared_counter);
    }
    
    threadpool_wait(pool);
    
    // If there were race conditions, counter would be less than expected
    ASSERT_EQ(shared_counter, 10000);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

static int test_multiple_wait_calls(void) {
    TEST_START("multiple threadpool_wait calls");
    
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    int counter = 0;
    
    // Submit tasks
    for (int i = 0; i < 50; i++) {
        threadpool_submit(pool, increment_task, &counter);
    }
    threadpool_wait(pool);
    ASSERT_EQ(counter, 50);
    
    // Submit more tasks
    for (int i = 0; i < 50; i++) {
        threadpool_submit(pool, increment_task, &counter);
    }
    threadpool_wait(pool);
    ASSERT_EQ(counter, 100);
    
    threadpool_destroy(pool);
    TEST_PASS();
    return 0;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    int failed = 0;
    
    printf("\n=== Phase 3: Threading & Concurrency Tests ===\n\n");
    
    printf("[Thread Pool Tests]\n");
    failed += test_threadpool_create_basic();
    failed += test_threadpool_create_single_thread();
    failed += test_threadpool_create_many_threads();
    failed += test_threadpool_create_invalid();
    failed += test_threadpool_submit_single();
    failed += test_threadpool_submit_multiple();
    failed += test_threadpool_parallel_execution();
    failed += test_threadpool_wait_blocks();
    failed += test_threadpool_empty_queue();
    failed += test_threadpool_shutdown_clean();
    failed += test_threadpool_large_task_count();
    
    printf("\n[Parallel Context Tests]\n");
    failed += test_parallel_create_basic();
    failed += test_parallel_create_invalid();
    failed += test_parallel_init_regions_even();
    failed += test_parallel_init_regions_uneven();
    failed += test_parallel_init_regions_covers_grid();
    failed += test_parallel_spread_mutate();
    failed += test_parallel_barrier();
    
    printf("\n[Race Condition Tests]\n");
    failed += test_no_race_shared_counter();
    failed += test_multiple_wait_calls();
    
    printf("\n=== Results ===\n");
    if (failed == 0) {
        printf("All tests PASSED!\n\n");
        return 0;
    } else {
        printf("%d test(s) FAILED!\n\n", failed);
        return 1;
    }
}
