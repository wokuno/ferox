#ifndef FEROX_SIMULATION_H
#define FEROX_SIMULATION_H

#include "world.h"

// Main simulation tick - advances world by one step
void simulation_tick(World* world);

// Spread colonies based on their spread rules
void simulation_spread(World* world);

// Apply mutations to all colonies
void simulation_mutate(World* world);

// Detect and handle colony divisions (flood-fill based)
void simulation_check_divisions(World* world);

// Detect and handle colony recombinations
void simulation_check_recombinations(World* world);

// Helper: flood-fill to find connected components of a colony
// Returns array of component sizes, sets cell markers
// Caller must free the returned array
int* find_connected_components(World* world, uint32_t colony_id, int* num_components);

// ============================================================================
// Parallel/Region-based processing functions
// ============================================================================

// Pending cell structure for double-buffered spreading (thread-safe)
typedef struct PendingCell {
    int x, y;
    uint32_t colony_id;
} PendingCell;

// Pending buffer structure (used for collecting spread results per region)
typedef struct PendingBuffer {
    PendingCell* cells;
    int count;
    int capacity;
} PendingBuffer;

// Create/destroy pending buffer for thread-local accumulation
PendingBuffer* pending_buffer_create(int initial_capacity);
void pending_buffer_destroy(PendingBuffer* buf);
void pending_buffer_add(PendingBuffer* buf, int x, int y, uint32_t colony_id);
void pending_buffer_clear(PendingBuffer* buf);

// Spread colonies within a region (thread-safe: writes to pending buffer, not world)
void simulation_spread_region(World* world, int start_x, int start_y, 
                              int end_x, int end_y, PendingBuffer* pending);

// Apply pending cells to world (must be called serially after all regions complete)
void simulation_apply_pending(World* world, PendingBuffer** buffers, int buffer_count);

// Mutate colonies within a region (operates on colony genome - need atomics)
void simulation_mutate_region(World* world, int start_x, int start_y, 
                              int end_x, int end_y);

// Age cells within a region (thread-safe)
void simulation_age_region(World* world, int start_x, int start_y, 
                           int end_x, int end_y);

// Update colony stats (wobble animation, etc) - can run in parallel per colony
void simulation_update_colony_stats(World* world);

// Combat resolution when colonies meet at borders
void simulation_resolve_combat(World* world);

// Update nutrients, toxins, and environmental layers
void simulation_update_nutrients(World* world);

#endif // FEROX_SIMULATION_H
