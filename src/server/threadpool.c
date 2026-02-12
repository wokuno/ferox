/**
 * threadpool.c - Thread pool implementation
 * Part of Phase 3: Threading & Concurrency
 */

#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>

// Worker thread function
static void* worker_thread(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Wait for a task or shutdown signal
        while (pool->task_queue_head == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }
        
        // Check for shutdown
        if (pool->shutdown && pool->task_queue_head == NULL) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }
        
        // Dequeue a task
        Task* task = pool->task_queue_head;
        if (task != NULL) {
            pool->task_queue_head = task->next;
            if (pool->task_queue_head == NULL) {
                pool->task_queue_tail = NULL;
            }
            pool->pending_tasks--;
            pool->active_tasks++;
        }
        
        pthread_mutex_unlock(&pool->queue_mutex);
        
        // Execute the task outside the lock
        if (task != NULL) {
            task->function(task->arg);
            free(task);
            
            pthread_mutex_lock(&pool->queue_mutex);
            pool->active_tasks--;
            
            // Signal if all tasks are done
            if (pool->active_tasks == 0 && pool->pending_tasks == 0) {
                pthread_cond_broadcast(&pool->done_cond);
            }
            pthread_mutex_unlock(&pool->queue_mutex);
        }
    }
    
    return NULL;
}

ThreadPool* threadpool_create(int num_threads) {
    if (num_threads <= 0) {
        return NULL;
    }
    
    ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
    if (pool == NULL) {
        return NULL;
    }
    
    pool->thread_count = num_threads;
    pool->task_queue_head = NULL;
    pool->task_queue_tail = NULL;
    pool->active_tasks = 0;
    pool->pending_tasks = 0;
    pool->shutdown = false;
    
    // Initialize synchronization primitives
    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0) {
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->done_cond, NULL) != 0) {
        pthread_cond_destroy(&pool->queue_cond);
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }
    
    // Allocate thread array
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    if (pool->threads == NULL) {
        pthread_cond_destroy(&pool->done_cond);
        pthread_cond_destroy(&pool->queue_cond);
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }
    
    // Create worker threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            // Failed to create thread, shutdown existing threads
            pool->shutdown = true;
            pthread_cond_broadcast(&pool->queue_cond);
            
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            
            free(pool->threads);
            pthread_cond_destroy(&pool->done_cond);
            pthread_cond_destroy(&pool->queue_cond);
            pthread_mutex_destroy(&pool->queue_mutex);
            free(pool);
            return NULL;
        }
    }
    
    return pool;
}

void threadpool_destroy(ThreadPool* pool) {
    if (pool == NULL) {
        return;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    // Signal shutdown
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->queue_cond);
    
    pthread_mutex_unlock(&pool->queue_mutex);
    
    // Wait for all threads to finish
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // Free any remaining tasks in the queue
    Task* task = pool->task_queue_head;
    while (task != NULL) {
        Task* next = task->next;
        free(task);
        task = next;
    }
    
    // Clean up resources
    free(pool->threads);
    pthread_cond_destroy(&pool->done_cond);
    pthread_cond_destroy(&pool->queue_cond);
    pthread_mutex_destroy(&pool->queue_mutex);
    free(pool);
}

void threadpool_submit(ThreadPool* pool, task_func func, void* arg) {
    if (pool == NULL || func == NULL) {
        return;
    }
    
    Task* task = (Task*)malloc(sizeof(Task));
    if (task == NULL) {
        return;
    }
    
    task->function = func;
    task->arg = arg;
    task->next = NULL;
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    // Don't accept new tasks if shutting down
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->queue_mutex);
        free(task);
        return;
    }
    
    // Enqueue the task
    if (pool->task_queue_tail == NULL) {
        pool->task_queue_head = task;
        pool->task_queue_tail = task;
    } else {
        pool->task_queue_tail->next = task;
        pool->task_queue_tail = task;
    }
    
    pool->pending_tasks++;
    
    // Wake up a worker thread
    pthread_cond_signal(&pool->queue_cond);
    
    pthread_mutex_unlock(&pool->queue_mutex);
}

void threadpool_wait(ThreadPool* pool) {
    if (pool == NULL) {
        return;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    // Wait until all tasks are complete
    while (pool->active_tasks > 0 || pool->pending_tasks > 0) {
        pthread_cond_wait(&pool->done_cond, &pool->queue_mutex);
    }
    
    pthread_mutex_unlock(&pool->queue_mutex);
}
