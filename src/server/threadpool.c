/**
 * threadpool.c - Thread pool implementation
 * Part of Phase 3: Threading & Concurrency
 */

#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>

#define TASK_FREELIST_LIMIT 4096

typedef struct WorkerLocalState {
    ThreadPool* pool;
    Task* submit_head;
    Task* submit_tail;
    Task* free_list;
    int free_count;
} WorkerLocalState;

static pthread_key_t worker_state_key;
static pthread_once_t worker_state_key_once = PTHREAD_ONCE_INIT;

static void threadpool_make_worker_state_key(void) {
    (void)pthread_key_create(&worker_state_key, NULL);
}

static WorkerLocalState* threadpool_current_worker_state(void) {
    pthread_once(&worker_state_key_once, threadpool_make_worker_state_key);
    return (WorkerLocalState*)pthread_getspecific(worker_state_key);
}

static void threadpool_set_worker_state(WorkerLocalState* state) {
    pthread_once(&worker_state_key_once, threadpool_make_worker_state_key);
    (void)pthread_setspecific(worker_state_key, state);
}

static Task* threadpool_take_free_task_local(WorkerLocalState* state) {
    if (state == NULL || state->free_list == NULL) {
        return NULL;
    }

    Task* task = state->free_list;
    state->free_list = task->next;
    task->next = NULL;
    state->free_count--;
    return task;
}

static void threadpool_recycle_task_local(WorkerLocalState* state, Task* task) {
    if (state == NULL || task == NULL) {
        return;
    }

    if (state->free_count < TASK_FREELIST_LIMIT) {
        task->next = state->free_list;
        state->free_list = task;
        state->free_count++;
        return;
    }

    free(task);
}

static void threadpool_enqueue_local_task(WorkerLocalState* state, Task* task) {
    if (state->submit_tail == NULL) {
        state->submit_head = task;
        state->submit_tail = task;
    } else {
        state->submit_tail->next = task;
        state->submit_tail = task;
    }
}

static Task* threadpool_pop_local_task(WorkerLocalState* state) {
    if (state == NULL || state->submit_head == NULL) {
        return NULL;
    }

    Task* task = state->submit_head;
    state->submit_head = task->next;
    if (state->submit_head == NULL) {
        state->submit_tail = NULL;
    }
    task->next = NULL;
    return task;
}

static Task* threadpool_take_free_task_locked(ThreadPool* pool) {
    Task* task = pool->task_free_list;
    if (task != NULL) {
        pool->task_free_list = task->next;
        task->next = NULL;
        pool->counters.task_free_count--;
    }
    return task;
}

static void threadpool_free_task_list(Task* head) {
    while (head != NULL) {
        Task* next = head->next;
        free(head);
        head = next;
    }
}

static void threadpool_execute_local_tasks(ThreadPool* pool, WorkerLocalState* state) {
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        Task* task = threadpool_pop_local_task(state);
        if (task != NULL) {
            pool->counters.pending_tasks--;
        }
        pthread_mutex_unlock(&pool->queue_mutex);

        if (task == NULL) {
            return;
        }

        task->function(task->arg);
        threadpool_recycle_task_local(state, task);
    }
}

// Worker thread function
static void* worker_thread(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    WorkerLocalState local_state = {
        .pool = pool,
        .submit_head = NULL,
        .submit_tail = NULL,
        .free_list = NULL,
        .free_count = 0,
    };
    threadpool_set_worker_state(&local_state);
    
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Wait for a task or shutdown signal
        while (pool->task_queue_head == NULL && !pool->counters.shutdown) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }
        
        // Check for shutdown
        if (pool->counters.shutdown && pool->task_queue_head == NULL) {
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
            pool->counters.pending_tasks--;
            pool->counters.active_tasks++;
        }
        
        pthread_mutex_unlock(&pool->queue_mutex);
        
        // Execute the task outside the lock
        if (task != NULL) {
            task->function(task->arg);
            threadpool_execute_local_tasks(pool, &local_state);

            pthread_mutex_lock(&pool->queue_mutex);
            pool->counters.active_tasks--;
            pthread_mutex_unlock(&pool->queue_mutex);

            threadpool_recycle_task_local(&local_state, task);

            pthread_mutex_lock(&pool->queue_mutex);
            
            // Signal if all tasks are done
            if (pool->counters.active_tasks == 0 && pool->counters.pending_tasks == 0) {
                pthread_cond_broadcast(&pool->done_cond);
            }
            pthread_mutex_unlock(&pool->queue_mutex);
        }
    }

    threadpool_set_worker_state(NULL);
    threadpool_free_task_list(local_state.submit_head);
    threadpool_free_task_list(local_state.free_list);
    
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
    pool->counters.active_tasks = 0;
    pool->counters.pending_tasks = 0;
    pool->counters.task_free_count = 0;
    pool->counters.shutdown = false;
    
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
            pool->counters.shutdown = true;
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
    pool->counters.shutdown = true;
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

    WorkerLocalState* worker_state = threadpool_current_worker_state();
    if (worker_state != NULL && worker_state->pool == pool) {
        Task* task = threadpool_take_free_task_local(worker_state);
        if (task == NULL) {
            task = (Task*)malloc(sizeof(Task));
            if (task == NULL) {
                return;
            }
        }

        task->function = func;
        task->arg = arg;
        task->next = NULL;

        pthread_mutex_lock(&pool->queue_mutex);
        if (pool->counters.shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            threadpool_recycle_task_local(worker_state, task);
            return;
        }
        pool->counters.pending_tasks++;
        pthread_mutex_unlock(&pool->queue_mutex);

        threadpool_enqueue_local_task(worker_state, task);
        return;
    }

    pthread_mutex_lock(&pool->queue_mutex);

    // Don't accept new tasks if shutting down
    if (pool->counters.shutdown) {
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
        if (pool->counters.shutdown) {
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
    
    pool->counters.pending_tasks++;

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
        if (pool->counters.shutdown) {
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
    if (pool->counters.shutdown) {
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

    pool->counters.pending_tasks += submitted;

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
    while (pool->counters.active_tasks > 0 || pool->counters.pending_tasks > 0) {
        pthread_cond_wait(&pool->done_cond, &pool->queue_mutex);
    }
    
    pthread_mutex_unlock(&pool->queue_mutex);
}
