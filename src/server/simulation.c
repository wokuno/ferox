#include "simulation.h"
#include "genetics.h"
#include "../shared/utils.h"
#include "../shared/names.h"
#include "../shared/colors.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Direction offsets for 4-connectivity (N, E, S, W)
static const int DX[] = {0, 1, 0, -1};
static const int DY[] = {-1, 0, 1, 0};

// Environmental constants
#define NUTRIENT_DEPLETION_RATE 0.05f   // Nutrients consumed per cell per tick
#define NUTRIENT_REGEN_RATE 0.002f      // Natural nutrient regeneration
#define TOXIN_DECAY_RATE 0.01f          // Toxin decay per tick
#define QUORUM_SENSING_RADIUS 3         // Radius for local density calculation

// Calculate local population density around a cell
static float calculate_local_density(World* world, int x, int y, uint32_t colony_id) {
    int count = 0;
    int total = 0;
    for (int dy = -QUORUM_SENSING_RADIUS; dy <= QUORUM_SENSING_RADIUS; dy++) {
        for (int dx = -QUORUM_SENSING_RADIUS; dx <= QUORUM_SENSING_RADIUS; dx++) {
            Cell* neighbor = world_get_cell(world, x + dx, y + dy);
            if (neighbor) {
                total++;
                if (neighbor->colony_id == colony_id) {
                    count++;
                }
            }
        }
    }
    return total > 0 ? (float)count / (float)total : 0.0f;
}

// Calculate environmental spread modifier for a target cell
static float calculate_env_spread_modifier(World* world, Colony* colony, int tx, int ty, int sx, int sy) {
    int target_idx = ty * world->width + tx;
    float modifier = 1.0f;
    
    // Chemotaxis: prefer higher nutrient levels
    float nutrient = world->nutrients[target_idx];
    modifier *= (1.0f + colony->genome.nutrient_sensitivity * (nutrient - 0.5f));
    
    // Toxin avoidance: avoid areas with toxins
    float toxin = world->toxins[target_idx];
    modifier *= (1.0f - colony->genome.toxin_sensitivity * toxin);
    
    // Edge preference: positive = prefer edges, negative = prefer center
    float edge_dist_x = fminf((float)tx, (float)(world->width - 1 - tx)) / (float)(world->width / 2);
    float edge_dist_y = fminf((float)ty, (float)(world->height - 1 - ty)) / (float)(world->height / 2);
    float edge_factor = 1.0f - fminf(edge_dist_x, edge_dist_y);
    modifier *= (1.0f + colony->genome.edge_affinity * (edge_factor - 0.5f));
    
    // Quorum sensing: reduce spread probability if local density exceeds threshold
    float local_density = calculate_local_density(world, sx, sy, colony->id);
    if (local_density > colony->genome.quorum_threshold) {
        float density_penalty = (local_density - colony->genome.quorum_threshold) * 
                                (1.0f - colony->genome.density_tolerance);
        modifier *= (1.0f - density_penalty);
    }
    
    return utils_clamp_f(modifier, 0.0f, 2.0f);
}

// Stack for iterative flood-fill
typedef struct {
    int* data;
    int top;
    int capacity;
} Stack;

static Stack* stack_create(int capacity) {
    Stack* s = (Stack*)malloc(sizeof(Stack));
    if (!s) return NULL;
    s->data = (int*)malloc(capacity * 2 * sizeof(int));  // x,y pairs
    s->top = 0;
    s->capacity = capacity * 2;
    return s;
}

static void stack_destroy(Stack* s) {
    if (s) {
        free(s->data);
        free(s);
    }
}

static void stack_push(Stack* s, int x, int y) {
    if (s->top + 2 > s->capacity) {
        s->capacity *= 2;
        s->data = (int*)realloc(s->data, s->capacity * sizeof(int));
    }
    s->data[s->top++] = x;
    s->data[s->top++] = y;
}

static bool stack_pop(Stack* s, int* x, int* y) {
    if (s->top < 2) return false;
    *y = s->data[--s->top];
    *x = s->data[--s->top];
    return true;
}

static bool stack_empty(Stack* s) {
    return s->top == 0;
}

// Flood-fill from a starting cell, marking all connected cells with component_id
static int flood_fill(World* world, int start_x, int start_y, uint32_t colony_id, int8_t comp_id) {
    Stack* stack = stack_create(world->width * world->height / 4);
    if (!stack) return 0;
    
    int count = 0;
    stack_push(stack, start_x, start_y);
    
    Cell* start_cell = world_get_cell(world, start_x, start_y);
    if (start_cell) {
        start_cell->component_id = comp_id;
    }
    
    while (!stack_empty(stack)) {
        int x, y;
        stack_pop(stack, &x, &y);
        count++;
        
        // Check all 4 neighbors
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d];
            int ny = y + DY[d];
            
            Cell* neighbor = world_get_cell(world, nx, ny);
            if (neighbor && neighbor->colony_id == colony_id && neighbor->component_id == -1) {
                neighbor->component_id = comp_id;
                stack_push(stack, nx, ny);
            }
        }
    }
    
    stack_destroy(stack);
    return count;
}

int* find_connected_components(World* world, uint32_t colony_id, int* num_components) {
    if (!world || !num_components || colony_id == 0) {
        if (num_components) *num_components = 0;
        return NULL;
    }
    
    // Reset component markers for this colony's cells
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == colony_id) {
            world->cells[i].component_id = -1;
        }
    }
    
    // Find components
    int* sizes = NULL;
    int count = 0;
    int capacity = 4;
    sizes = (int*)malloc(capacity * sizeof(int));
    if (!sizes) {
        *num_components = 0;
        return NULL;
    }
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (cell && cell->colony_id == colony_id && cell->component_id == -1) {
                // Start new component
                if (count >= capacity) {
                    capacity *= 2;
                    sizes = (int*)realloc(sizes, capacity * sizeof(int));
                }
                sizes[count] = flood_fill(world, x, y, colony_id, (int8_t)count);
                count++;
            }
        }
    }
    
    *num_components = count;
    return sizes;
}

void simulation_spread(World* world) {
    if (!world) return;
    
    // Create list of cells to colonize (avoid modifying while iterating)
    typedef struct { int x, y; uint32_t colony_id; } PendingCell;
    PendingCell* pending = NULL;
    int pending_count = 0;
    int pending_capacity = 64;
    pending = (PendingCell*)malloc(pending_capacity * sizeof(PendingCell));
    if (!pending) return;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony) continue;
            
            // Try to spread to neighbors based on spread_rate with environmental modifiers
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor) continue;
                
                if (neighbor->colony_id == 0) {
                    // Empty cell - calculate spread probability with environmental sensing
                    float env_modifier = calculate_env_spread_modifier(world, colony, nx, ny, x, y);
                    float spread_prob = colony->genome.spread_rate * colony->genome.metabolism * env_modifier;
                    
                    if (rand_float() < spread_prob) {
                        if (pending_count >= pending_capacity) {
                            pending_capacity *= 2;
                            pending = (PendingCell*)realloc(pending, pending_capacity * sizeof(PendingCell));
                        }
                        pending[pending_count++] = (PendingCell){nx, ny, cell->colony_id};
                    }
                }
            }
        }
    }
    
    // Apply pending colonizations
    for (int i = 0; i < pending_count; i++) {
        Cell* cell = world_get_cell(world, pending[i].x, pending[i].y);
        if (cell) {
            uint32_t old_colony = cell->colony_id;
            
            // Update old colony's cell count
            if (old_colony != 0) {
                Colony* old = world_get_colony(world, old_colony);
                if (old && old->cell_count > 0) {
                    old->cell_count--;
                }
            }
            
            // Colonize
            cell->colony_id = pending[i].colony_id;
            cell->age = 0;
            
            // Update new colony's cell count
            Colony* colony = world_get_colony(world, pending[i].colony_id);
            if (colony) {
                colony->cell_count++;
            }
        }
    }
    
    free(pending);
}

void simulation_mutate(World* world) {
    if (!world) return;
    
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) {
            genome_mutate(&world->colonies[i].genome);
        }
    }
}

void simulation_check_divisions(World* world) {
    if (!world) return;
    
    // Only process one division per tick to keep simulation stable
    bool division_occurred = false;
    
    for (size_t i = 0; i < world->colony_count && !division_occurred; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active || colony->cell_count < 2) continue;
        
        int num_components;
        int* sizes = find_connected_components(world, colony->id, &num_components);
        
        if (sizes && num_components > 1) {
            // Colony has split! Keep largest component, create new colonies for others
            int largest_idx = 0;
            int largest_size = sizes[0];
            for (int c = 1; c < num_components; c++) {
                if (sizes[c] > largest_size) {
                    largest_size = sizes[c];
                    largest_idx = c;
                }
            }
            
            // Create new colonies for non-largest components (min 5 cells to avoid tiny fragments)
            for (int c = 0; c < num_components; c++) {
                if (c == largest_idx) continue;
                if (sizes[c] < 5) continue;  // Don't create colonies from tiny fragments
                
                // Create new colony with mutated genome
                Colony new_colony;
                memset(&new_colony, 0, sizeof(Colony));
                new_colony.id = 0;
                generate_scientific_name(new_colony.name, sizeof(new_colony.name));
                new_colony.genome = colony->genome;
                genome_mutate(&new_colony.genome);
                new_colony.color = new_colony.genome.body_color;
                new_colony.cell_count = (size_t)sizes[c];
                new_colony.max_cell_count = (size_t)sizes[c];
                new_colony.active = true;
                new_colony.age = 0;
                new_colony.parent_id = colony->id;
                
                // Generate unique shape seed for procedural shape (inherit and mutate from parent)
                new_colony.shape_seed = colony->shape_seed ^ (uint32_t)rand() ^ ((uint32_t)rand() << 8);
                new_colony.wobble_phase = (float)(rand() % 628) / 100.0f;
                
                uint32_t new_id = world_add_colony(world, new_colony);
                
                // Update cells to belong to new colony
                for (int j = 0; j < world->width * world->height; j++) {
                    if (world->cells[j].colony_id == colony->id && 
                        world->cells[j].component_id == c) {
                        world->cells[j].colony_id = new_id;
                    }
                }
            }
            
            // Update original colony's cell count
            colony->cell_count = (size_t)largest_size;
            division_occurred = true;  // Only one division per tick
        }
        
        free(sizes);
    }
}

void simulation_check_recombinations(World* world) {
    if (!world) return;
    
    // Check for adjacent compatible colonies
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony_a = world_get_colony(world, cell->colony_id);
            if (!colony_a) continue;
            
            // Check right and down neighbors only (avoid double-checking)
            for (int d = 1; d <= 2; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor || neighbor->colony_id == 0 || neighbor->colony_id == cell->colony_id) continue;
                
                Colony* colony_b = world_get_colony(world, neighbor->colony_id);
                if (!colony_b) continue;
                
                // Recombination only happens between very closely related colonies
                // (e.g., recently divided colonies that reconnect)
                // This requires checking parent_id relationship
                if (colony_a->parent_id != colony_b->id && colony_b->parent_id != colony_a->id) {
                    // Not parent-child, also check if siblings (same parent)
                    if (colony_a->parent_id == 0 || colony_a->parent_id != colony_b->parent_id) {
                        continue;  // Not related, no merge
                    }
                }
                
                // Calculate genetic distance - must be very close for siblings to merge
                float distance = genome_distance(&colony_a->genome, &colony_b->genome);
                
                // Very strict threshold - only nearly identical genomes merge
                float threshold = 0.05f;
                
                // Apply merge_affinity bonus: average of both colonies' affinities
                float avg_affinity = (colony_a->genome.merge_affinity + colony_b->genome.merge_affinity) / 2.0f;
                threshold += avg_affinity * 0.1f;  // Max bonus of 0.03
                
                // Check genetic compatibility with adjusted threshold
                if (distance <= threshold) {
                    // Merge: smaller colony joins larger
                    Colony* larger = colony_a->cell_count >= colony_b->cell_count ? colony_a : colony_b;
                    Colony* smaller = colony_a->cell_count >= colony_b->cell_count ? colony_b : colony_a;
                    
                    // Merge genomes
                    larger->genome = genome_merge(&larger->genome, larger->cell_count,
                                                  &smaller->genome, smaller->cell_count);
                    
                    // Transfer cells
                    for (int j = 0; j < world->width * world->height; j++) {
                        if (world->cells[j].colony_id == smaller->id) {
                            world->cells[j].colony_id = larger->id;
                        }
                    }
                    
                    larger->cell_count += smaller->cell_count;
                    smaller->cell_count = 0;
                    smaller->active = false;
                    
                    return;  // Only one merge per tick to keep things stable
                }
            }
        }
    }
}

// ============================================================================
// Parallel/Region-based processing functions
// ============================================================================

PendingBuffer* pending_buffer_create(int initial_capacity) {
    PendingBuffer* buf = (PendingBuffer*)malloc(sizeof(PendingBuffer));
    if (!buf) return NULL;
    
    buf->cells = (PendingCell*)malloc(initial_capacity * sizeof(PendingCell));
    if (!buf->cells) {
        free(buf);
        return NULL;
    }
    buf->count = 0;
    buf->capacity = initial_capacity;
    return buf;
}

void pending_buffer_destroy(PendingBuffer* buf) {
    if (buf) {
        free(buf->cells);
        free(buf);
    }
}

void pending_buffer_add(PendingBuffer* buf, int x, int y, uint32_t colony_id) {
    if (!buf) return;
    
    if (buf->count >= buf->capacity) {
        buf->capacity *= 2;
        buf->cells = (PendingCell*)realloc(buf->cells, buf->capacity * sizeof(PendingCell));
        if (!buf->cells) return;
    }
    buf->cells[buf->count].x = x;
    buf->cells[buf->count].y = y;
    buf->cells[buf->count].colony_id = colony_id;
    buf->count++;
}

void pending_buffer_clear(PendingBuffer* buf) {
    if (buf) buf->count = 0;
}

void simulation_spread_region(World* world, int start_x, int start_y, 
                              int end_x, int end_y, PendingBuffer* pending) {
    if (!world || !pending) return;
    
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony) continue;
            
            // Try to spread to neighbors based on spread_rate
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor) continue;
                
                if (neighbor->colony_id == 0) {
                    // Empty cell - can spread
                    if (rand_float() < colony->genome.spread_rate * colony->genome.metabolism) {
                        pending_buffer_add(pending, nx, ny, cell->colony_id);
                    }
                } else if (neighbor->colony_id != cell->colony_id) {
                    // Enemy cell - might overtake based on aggression vs resilience
                    Colony* enemy = world_get_colony(world, neighbor->colony_id);
                    if (enemy && rand_float() < colony->genome.aggression * (1.0f - enemy->genome.resilience)) {
                        pending_buffer_add(pending, nx, ny, cell->colony_id);
                    }
                }
            }
        }
    }
}

void simulation_apply_pending(World* world, PendingBuffer** buffers, int buffer_count) {
    if (!world || !buffers) return;
    
    for (int b = 0; b < buffer_count; b++) {
        PendingBuffer* pending = buffers[b];
        if (!pending) continue;
        
        for (int i = 0; i < pending->count; i++) {
            Cell* cell = world_get_cell(world, pending->cells[i].x, pending->cells[i].y);
            if (cell) {
                uint32_t old_colony = cell->colony_id;
                
                // Update old colony's cell count
                if (old_colony != 0) {
                    Colony* old = world_get_colony(world, old_colony);
                    if (old && old->cell_count > 0) {
                        old->cell_count--;
                    }
                }
                
                // Colonize
                cell->colony_id = pending->cells[i].colony_id;
                cell->age = 0;
                
                // Update new colony's cell count
                Colony* colony = world_get_colony(world, pending->cells[i].colony_id);
                if (colony) {
                    colony->cell_count++;
                }
            }
        }
    }
}

void simulation_mutate_region(World* world, int start_x, int start_y, 
                              int end_x, int end_y) {
    (void)start_x; (void)start_y; (void)end_x; (void)end_y;
    // Mutations happen per-colony, not per-cell, so region doesn't matter
    // This is kept for API consistency; actual mutation happens in simulation_mutate
    if (!world) return;
}

void simulation_age_region(World* world, int start_x, int start_y, 
                           int end_x, int end_y) {
    if (!world) return;
    
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            int idx = y * world->width + x;
            if (world->cells[idx].colony_id != 0 && world->cells[idx].age < 255) {
                world->cells[idx].age++;
            }
        }
    }
}

void simulation_update_colony_stats(World* world) {
    if (!world) return;
    
    // First recount all cells from grid to ensure accuracy
    for (size_t i = 0; i < world->colony_count; i++) {
        world->colonies[i].cell_count = 0;
    }
    
    for (int j = 0; j < world->width * world->height; j++) {
        uint32_t cid = world->cells[j].colony_id;
        if (cid != 0) {
            Colony* col = world_get_colony(world, cid);
            if (col) {
                col->cell_count++;
            }
        }
    }
    
    // Now update stats
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        
        // Track max population
        if (colony->cell_count > colony->max_cell_count) {
            colony->max_cell_count = colony->cell_count;
        }
        
        // Mark as dead if population hits 0
        if (colony->cell_count == 0) {
            colony->active = false;
            continue;
        }
        
        // Animate wobble phase for organic movement
        colony->wobble_phase += 0.03f;
        if (colony->wobble_phase > 6.28318f) colony->wobble_phase -= 6.28318f;
        
        // Note: shape_seed is NOT mutated - that causes jarring visual jumps
        // Shape evolution happens naturally through smooth wobble_phase animation
    }
}

// ============================================================================
// Environmental Dynamics Functions
// ============================================================================

// Update nutrient levels: deplete where cells are, regenerate elsewhere
void simulation_update_nutrients(World* world) {
    if (!world || !world->nutrients) return;
    
    int total_cells = world->width * world->height;
    
    for (int i = 0; i < total_cells; i++) {
        if (world->cells[i].colony_id != 0) {
            // Cells consume nutrients based on metabolism
            Colony* colony = world_get_colony(world, world->cells[i].colony_id);
            float consumption = NUTRIENT_DEPLETION_RATE;
            if (colony) {
                consumption *= colony->genome.metabolism;
                // High efficiency reduces consumption
                consumption *= (1.0f - colony->genome.efficiency * 0.5f);
            }
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] - consumption, 0.0f, 1.0f);
        } else {
            // Empty cells slowly regenerate nutrients
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] + NUTRIENT_REGEN_RATE, 0.0f, 1.0f);
        }
    }
}

// Decay toxins over time
void simulation_decay_toxins(World* world) {
    if (!world || !world->toxins) return;
    
    int total_cells = world->width * world->height;
    for (int i = 0; i < total_cells; i++) {
        world->toxins[i] = utils_clamp_f(world->toxins[i] - TOXIN_DECAY_RATE, 0.0f, 1.0f);
    }
}

// ============================================================================
// Competitive Strategy Functions
// ============================================================================

// Produce toxins around colony borders
void simulation_produce_toxins(World* world) {
    if (!world || !world->toxins) return;
    
    // Decay existing toxins
    int total_cells = world->width * world->height;
    for (int i = 0; i < total_cells; i++) {
        world->toxins[i] *= 0.95f;  // 5% decay per tick
    }
    
    // Each border cell produces toxins
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            // Border cells produce toxins based on toxin_production trait
            // High defense_priority increases toxin output at borders
            float production = colony->genome.toxin_production * 
                               (1.0f + colony->genome.defense_priority * 0.5f);
            
            // Emit toxins to neighboring cells
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                if (nx < 0 || nx >= world->width || ny < 0 || ny >= world->height) continue;
                
                int idx = ny * world->width + nx;
                world->toxins[idx] = utils_clamp_f(world->toxins[idx] + production * 0.1f, 0.0f, 1.0f);
            }
        }
    }
}

// Apply toxin damage to cells
void simulation_apply_toxin_damage(World* world) {
    if (!world || !world->toxins) return;
    
    // Collect cells that should die from toxins
    typedef struct { int x, y; } DeadCell;
    DeadCell* dead = NULL;
    int dead_count = 0;
    int dead_capacity = 64;
    dead = (DeadCell*)malloc(dead_capacity * sizeof(DeadCell));
    if (!dead) return;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int idx = y * world->width + x;
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            float toxin_level = world->toxins[idx];
            if (toxin_level <= 0.01f) continue;
            
            // Damage = toxin_level * (1 - resistance) * vulnerability_factor
            // High defense_priority reduces damage at borders
            float vulnerability = cell->is_border ? 
                (1.0f - colony->genome.defense_priority * 0.3f) : 1.0f;
            float damage = toxin_level * (1.0f - colony->genome.toxin_resistance) * vulnerability;
            
            // Probabilistic death based on damage
            if (rand_float() < damage * 0.15f) {
                if (dead_count >= dead_capacity) {
                    dead_capacity *= 2;
                    dead = (DeadCell*)realloc(dead, dead_capacity * sizeof(DeadCell));
                    if (!dead) return;
                }
                dead[dead_count++] = (DeadCell){x, y};
            }
        }
    }
    
    // Kill cells
    for (int i = 0; i < dead_count; i++) {
        Cell* cell = world_get_cell(world, dead[i].x, dead[i].y);
        if (cell && cell->colony_id != 0) {
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (colony && colony->cell_count > 0) {
                colony->cell_count--;
            }
            cell->colony_id = 0;
            cell->age = 0;
            cell->is_border = false;
        }
    }
    
    free(dead);
}

// Handle resource consumption and nutrient depletion
void simulation_consume_resources(World* world) {
    if (!world || !world->nutrients) return;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int idx = y * world->width + x;
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            // Consume nutrients based on resource_consumption trait
            // High aggression also increases consumption
            float consumption = colony->genome.resource_consumption * 
                                (0.5f + colony->genome.aggression * 0.5f);
            
            // Deplete nutrients (but leave some minimum for regrowth)
            world->nutrients[idx] = utils_clamp_f(
                world->nutrients[idx] - consumption * 0.02f, 0.1f, 1.0f);
        }
    }
    
    // Slow nutrient regeneration in empty cells
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == 0) {
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] + 0.005f, 0.0f, 1.0f);
        }
    }
}

// Combat resolution when colonies meet at borders
void simulation_resolve_combat(World* world) {
    if (!world) return;
    
    typedef struct { int x, y; uint32_t winner; } CombatResult;
    CombatResult* results = NULL;
    int result_count = 0;
    int result_capacity = 64;
    results = (CombatResult*)malloc(result_capacity * sizeof(CombatResult));
    if (!results) return;
    
    // Check border cells for adjacent enemy cells
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* attacker = world_get_colony(world, cell->colony_id);
            if (!attacker || !attacker->active) continue;
            
            // Check neighbors for enemy cells
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor || neighbor->colony_id == 0 || 
                    neighbor->colony_id == cell->colony_id) continue;
                
                Colony* defender = world_get_colony(world, neighbor->colony_id);
                if (!defender || !defender->active) continue;
                
                // Combat calculation:
                // Attacker strength = aggression * (1 - defense_priority) + toxin_production
                // Defender strength = resilience * defense_priority + toxin_resistance
                float attack_str = attacker->genome.aggression * (1.0f - attacker->genome.defense_priority * 0.5f) +
                                   attacker->genome.toxin_production * 0.5f;
                float defend_str = defender->genome.resilience * (0.5f + defender->genome.defense_priority * 0.5f) +
                                   defender->genome.toxin_resistance * 0.3f;
                
                // Nutrient advantage: well-fed cells fight better
                if (world->nutrients) {
                    int attacker_idx = y * world->width + x;
                    int defender_idx = ny * world->width + nx;
                    attack_str *= (0.7f + world->nutrients[attacker_idx] * 0.3f);
                    defend_str *= (0.7f + world->nutrients[defender_idx] * 0.3f);
                }
                
                // Probabilistic outcome - no guaranteed winner
                float attack_chance = attack_str / (attack_str + defend_str + 0.5f);
                
                if (rand_float() < attack_chance * 0.1f) {
                    // Attacker wins this exchange
                    if (result_count >= result_capacity) {
                        result_capacity *= 2;
                        results = (CombatResult*)realloc(results, result_capacity * sizeof(CombatResult));
                        if (!results) return;
                    }
                    results[result_count++] = (CombatResult){nx, ny, cell->colony_id};
                }
            }
        }
    }
    
    // Apply combat results
    for (int i = 0; i < result_count; i++) {
        Cell* cell = world_get_cell(world, results[i].x, results[i].y);
        if (cell && cell->colony_id != 0 && cell->colony_id != results[i].winner) {
            // Defender loses this cell
            Colony* loser = world_get_colony(world, cell->colony_id);
            if (loser && loser->cell_count > 0) {
                loser->cell_count--;
            }
            // Cell becomes empty (not captured - simulates mutual destruction)
            cell->colony_id = 0;
            cell->age = 0;
            cell->is_border = false;
        }
    }
    
    free(results);
}

void simulation_tick(World* world) {
    if (!world) return;
    
    // Age all cells
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id != 0 && world->cells[i].age < 255) {
            world->cells[i].age++;
        }
    }
    
    // Update environmental layers
    simulation_update_nutrients(world);
    
    // Run simulation phases
    simulation_spread(world);
    simulation_mutate(world);
    simulation_check_divisions(world);
    simulation_check_recombinations(world);
    
    // Update colony stats and wobble animation
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        
        // Track max population
        if (colony->cell_count > colony->max_cell_count) {
            colony->max_cell_count = colony->cell_count;
        }
        
        // Mark as dead if population hits 0
        if (colony->cell_count == 0) {
            colony->active = false;
            continue;
        }
        
        // Animate wobble phase for organic movement
        colony->wobble_phase += 0.03f;
        if (colony->wobble_phase > 6.28318f) colony->wobble_phase -= 6.28318f;
        
        // Note: shape_seed is NOT mutated - that causes jarring visual jumps
        // Shape evolution happens naturally through smooth wobble_phase animation
    }
    
    world->tick++;
}
