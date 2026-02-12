/**
 * threadpool.h - Thread pool implementation for parallel task execution
 * Part of Phase 3: Threading & Concurrency
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stdbool.h>

// Task function type
typedef void (*task_func)(void* arg);

// Task structure
typedef struct Task {
    task_func function;
    void* arg;
    struct Task* next;
} Task;

// ThreadPool structure
typedef struct ThreadPool {
    pthread_t* threads;
    int thread_count;
    Task* task_queue_head;
    Task* task_queue_tail;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    pthread_cond_t done_cond;
    int active_tasks;
    int pending_tasks;
    bool shutdown;
} ThreadPool;

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
 * Wait for all submitted tasks to complete.
 * Blocks until both active_tasks and pending_tasks are zero.
 * @param pool The thread pool
 */
void threadpool_wait(ThreadPool* pool);

#endif // THREADPOOL_H
