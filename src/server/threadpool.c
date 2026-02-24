/**
 * threadpool.c - Thread pool implementation
 * Part of Phase 3: Threading & Concurrency
 */

#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>

#define TASK_FREELIST_LIMIT 4096

static Task* threadpool_take_free_task_locked(ThreadPool* pool) {
    Task* task = pool->task_free_list;
    if (task != NULL) {
        pool->task_free_list = task->next;
        task->next = NULL;
        pool->task_free_count--;
    }
    return task;
}

static void threadpool_recycle_task_locked(ThreadPool* pool, Task* task) {
    if (task == NULL) {
        return;
    }

    if (pool->task_free_count < TASK_FREELIST_LIMIT) {
        task->next = pool->task_free_list;
        pool->task_free_list = task;
        pool->task_free_count++;
        return;
    }

    free(task);
}

static void threadpool_free_task_list(Task* head) {
    while (head != NULL) {
        Task* next = head->next;
        free(head);
        head = next;
    }
}

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

            pthread_mutex_lock(&pool->queue_mutex);
            pool->active_tasks--;
            threadpool_recycle_task_locked(pool, task);
            
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
    pool->task_free_list = NULL;
    pool->active_tasks = 0;
    pool->pending_tasks = 0;
    pool->task_free_count = 0;
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

    Task* free_task = pool->task_free_list;
    while (free_task != NULL) {
        Task* next = free_task->next;
        free(free_task);
        free_task = next;
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

    pthread_mutex_lock(&pool->queue_mutex);

    // Don't accept new tasks if shutting down
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->queue_mutex);
        return;
    }

    Task* task = threadpool_take_free_task_locked(pool);
    if (task == NULL) {
        pthread_mutex_unlock(&pool->queue_mutex);

        task = (Task*)malloc(sizeof(Task));
        if (task == NULL) {
            return;
        }

        pthread_mutex_lock(&pool->queue_mutex);
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            free(task);
            return;
        }
    }

    task->function = func;
    task->arg = arg;
    task->next = NULL;

    // Enqueue the task
    if (pool->task_queue_tail == NULL) {
        pool->task_queue_head = task;
        pool->task_queue_tail = task;
    } else {
        pool->task_queue_tail->next = task;
        pool->task_queue_tail = task;
    }
    
    pool->pending_tasks++;

    // Wake a worker for each submit to maintain burst parallelism.
    pthread_cond_signal(&pool->queue_cond);
    
    pthread_mutex_unlock(&pool->queue_mutex);
}

void threadpool_submit_batch(ThreadPool* pool, task_func func, void* const* args, int count) {
    if (pool == NULL || func == NULL || args == NULL || count <= 0) {
        return;
    }

    Task* batch_head = NULL;
    Task* batch_tail = NULL;
    int submitted = 0;

    while (submitted < count) {
        pthread_mutex_lock(&pool->queue_mutex);
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }

        Task* task = threadpool_take_free_task_locked(pool);
        pthread_mutex_unlock(&pool->queue_mutex);

        if (task == NULL) {
            task = (Task*)malloc(sizeof(Task));
            if (task == NULL) {
                break;
            }
        }

        task->function = func;
        task->arg = (void*)args[submitted++];
        task->next = NULL;

        if (batch_tail == NULL) {
            batch_head = task;
            batch_tail = task;
        } else {
            batch_tail->next = task;
            batch_tail = task;
        }
    }

    if (batch_head == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->queue_mutex);
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->queue_mutex);
        threadpool_free_task_list(batch_head);
        return;
    }

    bool queue_was_empty = (pool->task_queue_head == NULL);

    if (pool->task_queue_tail == NULL) {
        pool->task_queue_head = batch_head;
        pool->task_queue_tail = batch_tail;
    } else {
        pool->task_queue_tail->next = batch_head;
        pool->task_queue_tail = batch_tail;
    }

    pool->pending_tasks += submitted;

    if (queue_was_empty) {
        if (submitted > 1) {
            pthread_cond_broadcast(&pool->queue_cond);
        } else {
            pthread_cond_signal(&pool->queue_cond);
        }
    }

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
