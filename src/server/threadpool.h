/**
 * threadpool.h - Thread pool implementation for parallel task execution
 * Part of Phase 3: Threading & Concurrency
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "../shared/cacheline.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

// Task function type
typedef void (*task_func)(void* arg);

// Task structure
typedef struct Task {
    task_func function;
    void* arg;
    struct Task* next;
} Task;

typedef struct {
    int active_tasks;
    int pending_tasks;
    int task_free_count;
    bool shutdown;
    uint8_t cacheline_padding[FEROX_CACHELINE_SIZE - (sizeof(int) * 3) - sizeof(bool)];
} ThreadPoolHotCounters;

_Static_assert(FEROX_CACHELINE_SIZE >= (int)((sizeof(int) * 3) + sizeof(bool)),
               "FEROX_CACHELINE_SIZE too small for ThreadPoolHotCounters");
_Static_assert(sizeof(ThreadPoolHotCounters) == FEROX_CACHELINE_SIZE,
               "ThreadPoolHotCounters should be one cacheline");

// ThreadPool structure
typedef struct ThreadPool {
    pthread_t* threads;
    int thread_count;
    Task* task_queue_head;
    Task* task_queue_tail;
    Task* task_free_list;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    pthread_cond_t done_cond;
    FEROX_CACHELINE_ALIGN ThreadPoolHotCounters counters;
} ThreadPool;

FEROX_CACHELINE_ASSERT_MEMBER_ALIGNED(ThreadPool, counters);

/**
 * Create a new thread pool with the specified number of worker threads.
 * @param num_threads Number of worker threads to create (must be > 0)
 * @return Pointer to the new ThreadPool, or NULL on failure
 */
ThreadPool* threadpool_create(int num_threads);

/**
 * Destroy the thread pool and free all resources.
 * Waits for all pending tasks to complete before shutting down.
 * @param pool The thread pool to destroy
 */
void threadpool_destroy(ThreadPool* pool);

/**
 * Submit a task to the thread pool for execution.
 * The task will be executed by one of the worker threads.
 * @param pool The thread pool
 * @param func The task function to execute
 * @param arg Argument to pass to the task function
 */
void threadpool_submit(ThreadPool* pool, task_func func, void* arg);

/**
 * Submit multiple tasks with the same function in one critical section.
 * Useful for hot loops that enqueue many small tasks.
 * @param pool The thread pool
 * @param func The task function to execute
 * @param args Array of task arguments, one per task
 * @param count Number of tasks in args
 */
void threadpool_submit_batch(ThreadPool* pool, task_func func, void* const* args, int count);

/**
 * Wait for all submitted tasks to complete.
 * Blocks until both active_tasks and pending_tasks are zero.
 * @param pool The thread pool
 */
void threadpool_wait(ThreadPool* pool);

#endif // THREADPOOL_H
