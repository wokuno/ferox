/**
 * test_threadpool_stress.c - Thread pool stress tests
 * Tests concurrent task processing, shutdown behavior, and correctness
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>

#include "../src/server/threadpool.h"

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
#define ASSERT_NULL(ptr) ASSERT((ptr) == NULL, #ptr " is NULL")
#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_GE(a, b) ASSERT((a) >= (b), #a " >= " #b)

// ============================================================================
// Test Helpers
// ============================================================================

// Atomic counter for task completion tracking
static atomic_int task_counter;

// Simple task that increments counter
static void increment_task(void* arg) {
    (void)arg;
    atomic_fetch_add(&task_counter, 1);
}

// Task that stores a result
typedef struct {
    int input;
    int result;
    int done;
} TaskData;

static void compute_task(void* arg) {
    TaskData* data = (TaskData*)arg;
    data->result = data->input * 2;
    data->done = 1;
}

// Task with varying execution time
static void variable_time_task(void* arg) {
    int* delay_us = (int*)arg;
    usleep(*delay_us);
    atomic_fetch_add(&task_counter, 1);
}

// ============================================================================
// Basic Stress Tests
// ============================================================================

TEST(ten_thousand_tasks_complete) {
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    atomic_store(&task_counter, 0);
    
    // Submit 10000 tasks
    for (int i = 0; i < 10000; i++) {
        threadpool_submit(pool, increment_task, NULL);
    }
    
    threadpool_wait(pool);
    
    ASSERT_EQ(atomic_load(&task_counter), 10000);
    
    threadpool_destroy(pool);
}

TEST(tasks_with_data) {
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    #define NUM_TASKS 1000
    TaskData tasks[NUM_TASKS];
    
    // Initialize and submit tasks
    for (int i = 0; i < NUM_TASKS; i++) {
        tasks[i].input = i;
        tasks[i].result = 0;
        tasks[i].done = 0;
        threadpool_submit(pool, compute_task, &tasks[i]);
    }
    
    threadpool_wait(pool);
    
    // Verify all results
    for (int i = 0; i < NUM_TASKS; i++) {
        ASSERT(tasks[i].done == 1, "Task not completed");
        ASSERT(tasks[i].result == i * 2, "Wrong result");
    }
    
    threadpool_destroy(pool);
    #undef NUM_TASKS
}

// ============================================================================
// Varying Execution Time Tests
// ============================================================================

TEST(tasks_varying_execution_times) {
    ThreadPool* pool = threadpool_create(8);
    ASSERT_NOT_NULL(pool);
    
    atomic_store(&task_counter, 0);
    
    // Create tasks with varying delays
    int delays[100];
    for (int i = 0; i < 100; i++) {
        delays[i] = (i % 10 + 1) * 100;  // 100us to 1000us
        threadpool_submit(pool, variable_time_task, &delays[i]);
    }
    
    threadpool_wait(pool);
    
    ASSERT_EQ(atomic_load(&task_counter), 100);
    
    threadpool_destroy(pool);
}

TEST(mixed_fast_slow_tasks) {
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    atomic_store(&task_counter, 0);
    
    int fast_delay = 10;
    int slow_delay = 10000;  // 10ms
    
    // Mix fast and slow tasks
    for (int i = 0; i < 50; i++) {
        if (i % 2 == 0) {
            threadpool_submit(pool, variable_time_task, &fast_delay);
        } else {
            threadpool_submit(pool, variable_time_task, &slow_delay);
        }
    }
    
    threadpool_wait(pool);
    
    ASSERT_EQ(atomic_load(&task_counter), 50);
    
    threadpool_destroy(pool);
}

// ============================================================================
// Rapid Submit/Wait Cycles
// ============================================================================

TEST(rapid_submit_wait_cycles) {
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    for (int cycle = 0; cycle < 100; cycle++) {
        atomic_store(&task_counter, 0);
        
        // Submit batch
        for (int i = 0; i < 10; i++) {
            threadpool_submit(pool, increment_task, NULL);
        }
        
        threadpool_wait(pool);
        
        ASSERT_EQ(atomic_load(&task_counter), 10);
    }
    
    threadpool_destroy(pool);
}

TEST(interleaved_submit_wait) {
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    atomic_store(&task_counter, 0);
    
    for (int i = 0; i < 50; i++) {
        threadpool_submit(pool, increment_task, NULL);
        
        // Wait every 5 tasks
        if ((i + 1) % 5 == 0) {
            threadpool_wait(pool);
        }
    }
    
    threadpool_wait(pool);
    ASSERT_EQ(atomic_load(&task_counter), 50);
    
    threadpool_destroy(pool);
}

// ============================================================================
// Concurrent Submit Tests
// ============================================================================

static ThreadPool* shared_pool;

static void* concurrent_submitter(void* arg) {
    int count = *(int*)arg;
    for (int i = 0; i < count; i++) {
        threadpool_submit(shared_pool, increment_task, NULL);
    }
    return NULL;
}

TEST(concurrent_submits_multiple_threads) {
    shared_pool = threadpool_create(4);
    ASSERT_NOT_NULL(shared_pool);
    
    atomic_store(&task_counter, 0);
    
    // Create multiple submitter threads
    pthread_t threads[4];
    int counts[4] = {250, 250, 250, 250};  // Total 1000 tasks
    
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, concurrent_submitter, &counts[i]);
    }
    
    // Wait for submitters to finish
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Wait for all tasks
    threadpool_wait(shared_pool);
    
    ASSERT_EQ(atomic_load(&task_counter), 1000);
    
    threadpool_destroy(shared_pool);
}

// ============================================================================
// Shutdown Tests
// ============================================================================

TEST(shutdown_with_pending_tasks) {
    ThreadPool* pool = threadpool_create(2);
    ASSERT_NOT_NULL(pool);
    
    atomic_store(&task_counter, 0);
    
    int delay = 50000;  // 50ms
    
    // Submit slow tasks
    for (int i = 0; i < 20; i++) {
        threadpool_submit(pool, variable_time_task, &delay);
    }
    
    // Don't wait, just destroy
    // Should wait for completion before destroying
    threadpool_destroy(pool);
    
    // Test passed if we get here without crash
    ASSERT_TRUE(1);
}

TEST(empty_pool_shutdown) {
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    // Immediately destroy empty pool
    threadpool_destroy(pool);
    
    ASSERT_TRUE(1);
}

TEST(shutdown_after_wait) {
    ThreadPool* pool = threadpool_create(4);
    ASSERT_NOT_NULL(pool);
    
    atomic_store(&task_counter, 0);
    
    for (int i = 0; i < 100; i++) {
        threadpool_submit(pool, increment_task, NULL);
    }
    
    threadpool_wait(pool);
    ASSERT_EQ(atomic_load(&task_counter), 100);
    
    threadpool_destroy(pool);
}

// ============================================================================
// Thread Count Tests
// ============================================================================

TEST(single_thread_pool) {
    ThreadPool* pool = threadpool_create(1);
    ASSERT_NOT_NULL(pool);
    
    atomic_store(&task_counter, 0);
    
    for (int i = 0; i < 100; i++) {
        threadpool_submit(pool, increment_task, NULL);
    }
    
    threadpool_wait(pool);
    ASSERT_EQ(atomic_load(&task_counter), 100);
    
    threadpool_destroy(pool);
}

TEST(many_threads_pool) {
    ThreadPool* pool = threadpool_create(16);
    ASSERT_NOT_NULL(pool);
    
    atomic_store(&task_counter, 0);
    
    for (int i = 0; i < 1000; i++) {
        threadpool_submit(pool, increment_task, NULL);
    }
    
    threadpool_wait(pool);
    ASSERT_EQ(atomic_load(&task_counter), 1000);
    
    threadpool_destroy(pool);
}

TEST(single_vs_many_threads_same_result) {
    // Single thread
    ThreadPool* pool1 = threadpool_create(1);
    ASSERT_NOT_NULL(pool1);
    
    atomic_store(&task_counter, 0);
    
    for (int i = 0; i < 500; i++) {
        threadpool_submit(pool1, increment_task, NULL);
    }
    
    threadpool_wait(pool1);
    int result1 = atomic_load(&task_counter);
    
    threadpool_destroy(pool1);
    
    // Many threads
    ThreadPool* pool2 = threadpool_create(8);
    ASSERT_NOT_NULL(pool2);
    
    atomic_store(&task_counter, 0);
    
    for (int i = 0; i < 500; i++) {
        threadpool_submit(pool2, increment_task, NULL);
    }
    
    threadpool_wait(pool2);
    int result2 = atomic_load(&task_counter);
    
    threadpool_destroy(pool2);
    
    // Both should produce same result
    ASSERT_EQ(result1, result2);
    ASSERT_EQ(result1, 500);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(null_pool_operations) {
    // These should handle NULL gracefully
    threadpool_submit(NULL, increment_task, NULL);
    threadpool_wait(NULL);
    threadpool_destroy(NULL);
    
    ASSERT_TRUE(1);
}

TEST(null_function) {
    ThreadPool* pool = threadpool_create(2);
    ASSERT_NOT_NULL(pool);
    
    // Submit with NULL function should be handled
    threadpool_submit(pool, NULL, NULL);
    
    threadpool_wait(pool);
    threadpool_destroy(pool);
    
    ASSERT_TRUE(1);
}

TEST(invalid_thread_count) {
    ThreadPool* pool = threadpool_create(0);
    ASSERT_NULL(pool);
    
    pool = threadpool_create(-1);
    ASSERT_NULL(pool);
}

TEST(multiple_pool_instances) {
    ThreadPool* pool1 = threadpool_create(2);
    ThreadPool* pool2 = threadpool_create(2);
    ThreadPool* pool3 = threadpool_create(2);
    
    ASSERT_NOT_NULL(pool1);
    ASSERT_NOT_NULL(pool2);
    ASSERT_NOT_NULL(pool3);
    
    atomic_store(&task_counter, 0);
    
    // Submit to all pools
    for (int i = 0; i < 100; i++) {
        threadpool_submit(pool1, increment_task, NULL);
        threadpool_submit(pool2, increment_task, NULL);
        threadpool_submit(pool3, increment_task, NULL);
    }
    
    threadpool_wait(pool1);
    threadpool_wait(pool2);
    threadpool_wait(pool3);
    
    ASSERT_EQ(atomic_load(&task_counter), 300);
    
    threadpool_destroy(pool1);
    threadpool_destroy(pool2);
    threadpool_destroy(pool3);
}

// ============================================================================
// Run Tests
// ============================================================================

int run_threadpool_stress_tests(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    printf("\n=== Thread Pool Stress Tests ===\n\n");
    
    printf("Basic Stress Tests:\n");
    RUN_TEST(ten_thousand_tasks_complete);
    RUN_TEST(tasks_with_data);
    
    printf("\nVarying Execution Time Tests:\n");
    RUN_TEST(tasks_varying_execution_times);
    RUN_TEST(mixed_fast_slow_tasks);
    
    printf("\nRapid Submit/Wait Cycles:\n");
    RUN_TEST(rapid_submit_wait_cycles);
    RUN_TEST(interleaved_submit_wait);
    
    printf("\nConcurrent Submit Tests:\n");
    RUN_TEST(concurrent_submits_multiple_threads);
    
    printf("\nShutdown Tests:\n");
    RUN_TEST(shutdown_with_pending_tasks);
    RUN_TEST(empty_pool_shutdown);
    RUN_TEST(shutdown_after_wait);
    
    printf("\nThread Count Tests:\n");
    RUN_TEST(single_thread_pool);
    RUN_TEST(many_threads_pool);
    RUN_TEST(single_vs_many_threads_same_result);
    
    printf("\nEdge Cases:\n");
    RUN_TEST(null_pool_operations);
    RUN_TEST(null_function);
    RUN_TEST(invalid_thread_count);
    RUN_TEST(multiple_pool_instances);
    
    printf("\n--- Thread Pool Stress Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_threadpool_stress_tests() > 0 ? 1 : 0;
}
#endif
