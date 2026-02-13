/**
 * parallel.c - Parallel world update orchestration implementation
 * Part of Phase 3: Threading & Concurrency
 */

#include "parallel.h"
#include "simulation.h"
#include <stdlib.h>
#include <stdio.h>

// Task argument structure for region processing
typedef struct RegionTask {
    ParallelContext* ctx;
    Region* region;
    int region_index;
} RegionTask;

// Spread task function - process spreading for a region
static void spread_task(void* arg) {
    RegionTask* task = (RegionTask*)arg;
    World* world = task->ctx->world;
    Region* region = task->region;
    int idx = task->region_index;
    
    // Use pre-allocated pending buffer for this region
    PendingBuffer* pending = task->ctx->pending_buffers[idx];
    if (pending) {
        pending_buffer_clear(pending);
        simulation_spread_region(world, region->start_x, region->start_y,
                                 region->end_x, region->end_y, pending);
    }
    
    free(task);
}

// Age task function - age cells in a region
static void age_task(void* arg) {
    RegionTask* task = (RegionTask*)arg;
    World* world = task->ctx->world;
    Region* region = task->region;
    
    simulation_age_region(world, region->start_x, region->start_y,
                          region->end_x, region->end_y);
    
    free(task);
}

// Mutate task function (mutations are per-colony, kept for API completeness)
static void mutate_task(void* arg) {
    RegionTask* task = (RegionTask*)arg;
    (void)task;
    // Mutations happen per-colony in simulation_mutate(), not per-region
    free(task);
}

ParallelContext* parallel_create(ThreadPool* pool, World* world, int regions_x, int regions_y) {
    if (pool == NULL || regions_x <= 0 || regions_y <= 0) {
        return NULL;
    }
    
    ParallelContext* ctx = (ParallelContext*)malloc(sizeof(ParallelContext));
    if (ctx == NULL) {
        return NULL;
    }
    
    int region_count = regions_x * regions_y;
    
    ctx->pool = pool;
    ctx->world = world;
    ctx->regions_x = regions_x;
    ctx->regions_y = regions_y;
    ctx->region_count = region_count;
    
    // Initialize task mutex
    if (pthread_mutex_init(&ctx->task_mutex, NULL) != 0) {
        free(ctx);
        return NULL;
    }
    
    // Allocate regions array
    ctx->regions = (Region*)malloc(sizeof(Region) * region_count);
    if (ctx->regions == NULL) {
        free(ctx);
        return NULL;
    }
    
    // Allocate pending buffers array (one per region)
    ctx->pending_buffers = (PendingBuffer**)malloc(sizeof(PendingBuffer*) * region_count);
    if (ctx->pending_buffers == NULL) {
        free(ctx->regions);
        free(ctx);
        return NULL;
    }
    
    // Initialize regions and pending buffers
    for (int i = 0; i < region_count; i++) {
        ctx->regions[i].start_x = 0;
        ctx->regions[i].start_y = 0;
        ctx->regions[i].end_x = 0;
        ctx->regions[i].end_y = 0;
        ctx->pending_buffers[i] = pending_buffer_create(256);
    }
    
    return ctx;
}

void parallel_destroy(ParallelContext* ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // Free pending buffers
    if (ctx->pending_buffers) {
        for (int i = 0; i < ctx->region_count; i++) {
            if (ctx->pending_buffers[i]) {
                pending_buffer_destroy(ctx->pending_buffers[i]);
            }
        }
        free(ctx->pending_buffers);
    }
    
    pthread_mutex_destroy(&ctx->task_mutex);
    free(ctx->regions);
    free(ctx);
}

void parallel_init_regions(ParallelContext* ctx, int world_width, int world_height) {
    if (ctx == NULL || world_width <= 0 || world_height <= 0) {
        return;
    }
    
    int region_width = world_width / ctx->regions_x;
    int region_height = world_height / ctx->regions_y;
    
    // Handle remainder cells by adding them to the last regions
    int extra_width = world_width % ctx->regions_x;
    int extra_height = world_height % ctx->regions_y;
    
    int current_y = 0;
    for (int ry = 0; ry < ctx->regions_y; ry++) {
        int height = region_height + (ry < extra_height ? 1 : 0);
        int current_x = 0;
        
        for (int rx = 0; rx < ctx->regions_x; rx++) {
            int width = region_width + (rx < extra_width ? 1 : 0);
            int index = ry * ctx->regions_x + rx;
            
            ctx->regions[index].start_x = current_x;
            ctx->regions[index].start_y = current_y;
            ctx->regions[index].end_x = current_x + width;
            ctx->regions[index].end_y = current_y + height;
            
            current_x += width;
        }
        current_y += height;
    }
}

void parallel_age(ParallelContext* ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // Submit an age task for each region
    for (int i = 0; i < ctx->region_count; i++) {
        pthread_mutex_lock(&ctx->task_mutex);
        RegionTask* task = (RegionTask*)malloc(sizeof(RegionTask));
        if (task == NULL) {
            pthread_mutex_unlock(&ctx->task_mutex);
            fprintf(stderr, "Warning: Failed to allocate age task for region %d\n", i);
            continue;
        }
        
        task->ctx = ctx;
        task->region = &ctx->regions[i];
        task->region_index = i;
        
        threadpool_submit(ctx->pool, age_task, task);
        pthread_mutex_unlock(&ctx->task_mutex);
    }
}

void parallel_spread(ParallelContext* ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // Submit a spread task for each region
    for (int i = 0; i < ctx->region_count; i++) {
        pthread_mutex_lock(&ctx->task_mutex);
        RegionTask* task = (RegionTask*)malloc(sizeof(RegionTask));
        if (task == NULL) {
            pthread_mutex_unlock(&ctx->task_mutex);
            fprintf(stderr, "Warning: Failed to allocate spread task for region %d\n", i);
            continue;
        }
        
        task->ctx = ctx;
        task->region = &ctx->regions[i];
        task->region_index = i;
        
        threadpool_submit(ctx->pool, spread_task, task);
        pthread_mutex_unlock(&ctx->task_mutex);
    }
}

void parallel_mutate(ParallelContext* ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // Submit a mutate task for each region
    for (int i = 0; i < ctx->region_count; i++) {
        pthread_mutex_lock(&ctx->task_mutex);
        RegionTask* task = (RegionTask*)malloc(sizeof(RegionTask));
        if (task == NULL) {
            pthread_mutex_unlock(&ctx->task_mutex);
            fprintf(stderr, "Warning: Failed to allocate mutate task for region %d\n", i);
            continue;
        }
        
        task->ctx = ctx;
        task->region = &ctx->regions[i];
        task->region_index = i;
        
        threadpool_submit(ctx->pool, mutate_task, task);
        pthread_mutex_unlock(&ctx->task_mutex);
    }
}

void parallel_barrier(ParallelContext* ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // Wait for all submitted tasks to complete
    threadpool_wait(ctx->pool);
}

void parallel_tick(ParallelContext* ctx) {
    if (ctx == NULL || ctx->world == NULL) {
        return;
    }
    
    World* world = ctx->world;
    
    // Phase 1: Age cells in parallel
    parallel_age(ctx);
    parallel_barrier(ctx);
    
    // Phase 2: Spread colonies in parallel (writes to pending buffers)
    parallel_spread(ctx);
    parallel_barrier(ctx);
    
    // Phase 3: Apply pending changes serially (avoids race conditions)
    simulation_apply_pending(world, ctx->pending_buffers, ctx->region_count);
    
    // Phase 4: Mutations (per-colony, serial for now due to small number)
    simulation_mutate(world);
    
    // Phase 5: Division detection (serial - complex flood-fill)
    simulation_check_divisions(world);
    
    // Phase 6: Recombination detection (serial - modifies multiple colonies)
    simulation_check_recombinations(world);
    
    // Phase 7: Update colony stats
    simulation_update_colony_stats(world);
    
    world->tick++;
}
