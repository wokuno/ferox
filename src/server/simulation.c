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
    
    return utils_clamp_f(modifier, 0.3f, 2.0f);  // Minimum floor of 0.3 to prevent stalling
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
        int new_capacity = s->capacity * 2;
        int* new_data = (int*)realloc(s->data, new_capacity * sizeof(int));
        if (!new_data) {
            // Realloc failed - cannot push, this will cause flood-fill to be incomplete
            // but avoids a crash. The caller should handle incomplete results.
            return;
        }
        s->data = new_data;
        s->capacity = new_capacity;
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
    // Note: component_id is int8_t (-128 to 127), so we can track at most 127 components (0-126)
    // In practice, colonies rarely have more than a few components
    const int MAX_COMPONENTS = 127;
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
                // Start new component - but stop if we hit the int8_t limit
                if (count >= MAX_COMPONENTS) {
                    // Too many components to track safely, return what we have
                    // Remaining cells will be processed on next tick
                    *num_components = count;
                    return sizes;
                }
                if (count >= capacity) {
                    int new_capacity = capacity * 2;
                    if (new_capacity > MAX_COMPONENTS) new_capacity = MAX_COMPONENTS;
                    int* new_sizes = (int*)realloc(sizes, new_capacity * sizeof(int));
                    if (!new_sizes) {
                        // Realloc failed - return what we have
                        *num_components = count;
                        return sizes;
                    }
                    sizes = new_sizes;
                    capacity = new_capacity;
                }
                sizes[count] = flood_fill(world, x, y, colony_id, (int8_t)count);
                count++;
            }
        }
    }
    
    *num_components = count;
    return sizes;
}

// Count friendly neighbors around a cell (for flanking calculation)
static int count_friendly_neighbors(World* world, int x, int y, uint32_t colony_id) {
    int count = 0;
    for (int d = 0; d < 4; d++) {
        Cell* n = world_get_cell(world, x + DX[d], y + DY[d]);
        if (n && n->colony_id == colony_id) count++;
    }
    return count;
}

// Count enemy neighbors around a cell (for pressure calculation)
static int count_enemy_neighbors(World* world, int x, int y, uint32_t colony_id) {
    int count = 0;
    for (int d = 0; d < 4; d++) {
        Cell* n = world_get_cell(world, x + DX[d], y + DY[d]);
        if (n && n->colony_id != 0 && n->colony_id != colony_id) count++;
    }
    return count;
}

// Get directional weight for spread_weights (maps 4-direction to 8-direction weights)
static float get_direction_weight(Genome* g, int dx, int dy) {
    // Map dx,dy to direction index
    // N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7
    int dir = -1;
    if (dy == -1 && dx == 0) dir = 0;      // N
    else if (dy == 0 && dx == 1) dir = 2;  // E
    else if (dy == 1 && dx == 0) dir = 4;  // S
    else if (dy == 0 && dx == -1) dir = 6; // W
    return dir >= 0 ? g->spread_weights[dir] : 0.5f;
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
                    
                    // Directional preference from genome
                    float dir_weight = get_direction_weight(&colony->genome, DX[d], DY[d]);
                    
                    // Strategic spread: push harder towards open space, less where enemies are
                    int enemy_count = count_enemy_neighbors(world, nx, ny, cell->colony_id);
                    float strategic_modifier = 1.0f;
                    if (enemy_count > 0) {
                        // Slow down if target is contested (unless very aggressive)
                        strategic_modifier *= (0.3f + colony->genome.aggression * 0.4f);
                    }
                    
                    // Success history affects spread direction
                    float history_bonus = 1.0f + colony->success_history[d % 8] * 0.3f;
                    
                    // Very aggressive spread - colonies should constantly push
                    // Base multiplier of 5x to ensure colonies are always expanding
                    float spread_prob = colony->genome.spread_rate * colony->genome.metabolism * 
                                        env_modifier * dir_weight * strategic_modifier * history_bonus * 5.0f;
                    
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
    
    // Apply pending colonizations - this is where new cells are "born"
    // Mutations happen during cell division (new cell creation)
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
            
            // Update new colony's cell count and potentially mutate
            Colony* colony = world_get_colony(world, pending[i].colony_id);
            if (colony) {
                colony->cell_count++;
                
                // MUTATION ON REPRODUCTION: Each new cell has a chance to cause colony mutation
                // Higher stress = more mutations (adaptation pressure)
                float mutation_chance = colony->genome.mutation_rate * 
                                        (1.0f + colony->stress_level * 2.0f);
                if (rand_float() < mutation_chance) {
                    genome_mutate(&colony->genome);
                }
            }
        }
    }
    
    free(pending);
}

void simulation_mutate(World* world) {
    // Mutations happen during cell division AND as constant background process
    // This ensures evolution is always visible and dynamic
    if (!world) return;
    
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        
        // Higher baseline mutation - evolution should be visible
        // Minimum 8% chance + mutation_rate contribution
        float baseline_rate = 0.08f + colony->genome.mutation_rate * 0.6f;
        
        // Stressed colonies mutate much more as they try to adapt
        baseline_rate *= (1.0f + colony->stress_level * 2.5f);
        
        // Larger colonies have more chances to mutate (more cells dividing)
        baseline_rate *= (1.0f + (float)colony->cell_count / 300.0f);
        
        if (rand_float() < baseline_rate) {
            // Store original genome for speciation check
            Genome original = colony->genome;
            
            // Apply mutation
            genome_mutate(&colony->genome);
            
            // Update colony color to reflect genome changes
            colony->color = colony->genome.body_color;
            
            // SPECIATION EVENT: If mutation was extreme, create a new species
            // Check how different the genome became
            float genetic_distance = genome_distance(&original, &colony->genome);
            
            // 5% base chance of speciation, higher if mutation was dramatic
            float speciation_chance = 0.05f + genetic_distance * 0.3f;
            
            // More likely for larger colonies (more genetic diversity)
            if (colony->cell_count > 30) {
                speciation_chance *= 1.5f;
            }
            
            if (rand_float() < speciation_chance && colony->cell_count > 10) {
                // SPECIATION! Split off some cells as a new species
                // The new species gets the mutated genome, parent keeps original
                
                int cells_for_new = (int)(colony->cell_count * (0.2f + rand_float() * 0.3f));  // 20-50%
                if (cells_for_new >= 5) {
                    // Create new colony with mutated genome
                    Colony new_species;
                    memset(&new_species, 0, sizeof(Colony));
                    new_species.genome = colony->genome;  // Mutated genome goes to new species
                    new_species.color = new_species.genome.body_color;
                    new_species.active = true;
                    new_species.parent_id = colony->id;
                    new_species.shape_seed = colony->shape_seed ^ (uint32_t)(rand() << 8);
                    new_species.wobble_phase = rand_float() * 6.28f;
                    new_species.cell_count = 0;  // Will be set by cell transfer
                    new_species.max_cell_count = 0;
                    generate_scientific_name(new_species.name, sizeof(new_species.name));
                    
                    // Revert parent to original genome
                    colony->genome = original;
                    colony->color = original.body_color;
                    
                    uint32_t new_id = world_add_colony(world, new_species);
                    if (new_id > 0) {
                        // Transfer some cells to new species (border cells preferentially)
                        int transferred = 0;
                        for (int j = 0; j < world->width * world->height && transferred < cells_for_new; j++) {
                            Cell* cell = &world->cells[j];
                            if (cell->colony_id == colony->id) {
                                // Prefer border cells for speciation
                                float transfer_chance = cell->is_border ? 0.6f : 0.3f;
                                if (rand_float() < transfer_chance) {
                                    cell->colony_id = new_id;
                                    cell->age = 0;  // New species cells are "young"
                                    transferred++;
                                    if (colony->cell_count > 0) colony->cell_count--;
                                }
                            }
                        }
                        // Update new species cell count
                        Colony* new_col = world_get_colony(world, new_id);
                        if (new_col) {
                            new_col->cell_count = transferred;
                            new_col->max_cell_count = transferred;
                        }
                    }
                }
            }
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
            // Track cells that get orphaned (tiny fragments)
            int orphaned_cells = 0;
            
            for (int c = 0; c < num_components; c++) {
                if (c == largest_idx) continue;
                if (sizes[c] < 5) {
                    // Tiny fragment - these cells will be orphaned (removed)
                    // Clear these cells from the grid
                    for (int j = 0; j < world->width * world->height; j++) {
                        if (world->cells[j].colony_id == colony->id && 
                            world->cells[j].component_id == c) {
                            world->cells[j].colony_id = 0;
                            world->cells[j].age = 0;
                            world->cells[j].is_border = false;
                            orphaned_cells++;
                        }
                    }
                    continue;
                }
                
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
            
            // Update original colony's cell count to largest component only
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
                    // Empty cell - can spread (very aggressive)
                    float spread_chance = colony->genome.spread_rate * colony->genome.metabolism * 6.0f;
                    if (rand_float() < spread_chance) {
                        pending_buffer_add(pending, nx, ny, cell->colony_id);
                    }
                } else if (neighbor->colony_id != cell->colony_id) {
                    // Enemy cell - aggressive takeover
                    Colony* enemy = world_get_colony(world, neighbor->colony_id);
                    if (enemy && enemy->active) {
                        // Very aggressive combat
                        float attack = colony->genome.aggression * (1.0f + colony->genome.toxin_production * 0.7f);
                        float defense = enemy->genome.resilience * (0.4f + enemy->genome.defense_priority * 0.4f);
                        float combat_chance = attack / (attack + defense + 0.05f);
                        // Always at least 50% chance to attack, boosted by combat_chance
                        if (rand_float() < 0.5f + combat_chance * 0.5f) {
                            pending_buffer_add(pending, nx, ny, cell->colony_id);
                        }
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
            
            // Deplete nutrients more aggressively - creates starvation pressure
            world->nutrients[idx] = utils_clamp_f(
                world->nutrients[idx] - consumption * 0.05f, 0.0f, 1.0f);
        }
    }
    
    // Nutrient regeneration - faster in empty cells, slower in occupied
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == 0) {
            // Empty cells regenerate quickly
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] + 0.02f, 0.0f, 1.0f);
        } else {
            // Occupied cells regenerate very slowly
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] + 0.002f, 0.0f, 1.0f);
        }
    }
    
    // Toxin decay
    for (int i = 0; i < world->width * world->height; i++) {
        world->toxins[i] = utils_clamp_f(world->toxins[i] - 0.01f, 0.0f, 1.0f);
    }
}

// Combat resolution when colonies meet at borders
void simulation_resolve_combat(World* world) {
    if (!world) return;
    
    // Decay existing toxins
    if (world->toxins) {
        int total = world->width * world->height;
        for (int i = 0; i < total; i++) {
            world->toxins[i] *= 0.95f;  // 5% decay per tick
        }
    }
    
    typedef struct { int x, y; uint32_t winner; uint32_t loser; float strength; } CombatResult;
    CombatResult* results = NULL;
    int result_count = 0;
    int result_capacity = 128;
    results = (CombatResult*)malloc(result_capacity * sizeof(CombatResult));
    if (!results) return;
    
    // First pass: emit toxins from aggressive colonies to create hostile zones
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            // Border cells emit toxins based on toxin_production
            float toxin_emit = colony->genome.toxin_production * 0.1f;
            if (toxin_emit > 0.01f) {
                // Emit to self and neighbors
                int idx = y * world->width + x;
                world->toxins[idx] = utils_clamp_f(world->toxins[idx] + toxin_emit, 0.0f, 1.0f);
                for (int d = 0; d < 4; d++) {
                    int nx = x + DX[d], ny = y + DY[d];
                    if (nx >= 0 && nx < world->width && ny >= 0 && ny < world->height) {
                        int ni = ny * world->width + nx;
                        world->toxins[ni] = utils_clamp_f(world->toxins[ni] + toxin_emit * 0.5f, 0.0f, 1.0f);
                    }
                }
            }
        }
    }
    
    // Second pass: resolve combat at borders with strategic modifiers
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* attacker = world_get_colony(world, cell->colony_id);
            if (!attacker || !attacker->active) continue;
            
            // Skip if colony is in retreat mode (stressed and defensive)
            if (attacker->stress_level > 0.7f && attacker->genome.defense_priority > 0.6f) {
                continue; // Defensive colonies under stress don't attack
            }
            
            int attacker_friendly = count_friendly_neighbors(world, x, y, cell->colony_id);
            
            // Check neighbors for enemy cells
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor || neighbor->colony_id == 0 || 
                    neighbor->colony_id == cell->colony_id) continue;
                
                Colony* defender = world_get_colony(world, neighbor->colony_id);
                if (!defender || !defender->active) continue;
                
                // === STRATEGIC COMBAT CALCULATION ===
                
                // Base strength from genome
                float attack_str = attacker->genome.aggression * 1.2f;
                float defend_str = defender->genome.resilience * 1.0f;
                
                // 1. FLANKING BONUS: More friendly neighbors = stronger attack
                float flanking_bonus = 1.0f + (attacker_friendly * 0.15f);
                attack_str *= flanking_bonus;
                
                // 2. DEFENSIVE FORMATION: Defenders with high defense_priority are harder to crack
                int defender_friendly = count_friendly_neighbors(world, nx, ny, neighbor->colony_id);
                float defensive_bonus = 1.0f + (defender->genome.defense_priority * defender_friendly * 0.2f);
                defend_str *= defensive_bonus;
                
                // 3. DIRECTIONAL PREFERENCE: Colonies fight harder in preferred directions
                float dir_weight = get_direction_weight(&attacker->genome, DX[d], DY[d]);
                attack_str *= (0.7f + dir_weight * 0.6f); // 0.7-1.3x based on preferred direction
                
                // 4. TOXIN WARFARE: Toxin production aids attack, resistance aids defense
                attack_str += attacker->genome.toxin_production * 0.4f;
                defend_str += defender->genome.toxin_resistance * 0.3f;
                
                // 5. BIOFILM DEFENSE: Defenders with biofilm are harder to defeat
                defend_str *= (1.0f + defender->biofilm_strength * 0.3f);
                
                // 6. NUTRIENT ADVANTAGE: Well-fed cells fight better
                if (world->nutrients) {
                    int attacker_idx = y * world->width + x;
                    int defender_idx = ny * world->width + nx;
                    attack_str *= (0.6f + world->nutrients[attacker_idx] * 0.5f);
                    defend_str *= (0.6f + world->nutrients[defender_idx] * 0.5f);
                    
                    // Toxin damage reduces effectiveness
                    attack_str *= (1.0f - world->toxins[attacker_idx] * (1.0f - attacker->genome.toxin_resistance));
                    defend_str *= (1.0f - world->toxins[defender_idx] * (1.0f - defender->genome.toxin_resistance));
                }
                
                // 7. MOMENTUM: Colonies that have been winning keep winning (success_history)
                attack_str *= (0.8f + attacker->success_history[d] * 0.4f);
                
                // 8. STRESSED COLONIES FIGHT DIFFERENTLY
                if (attacker->stress_level > 0.5f) {
                    // Desperate attacks: higher risk, higher reward
                    attack_str *= (1.0f + attacker->genome.aggression * 0.3f);
                }
                if (defender->stress_level > 0.5f && defender->genome.defense_priority < 0.4f) {
                    // Stressed non-defensive colonies crumble
                    defend_str *= 0.7f;
                }
                
                // 9. SIZE MATTERS: Larger colonies have morale advantage
                float size_ratio = (float)attacker->cell_count / (float)(defender->cell_count + 1);
                if (size_ratio > 2.0f) attack_str *= 1.15f;  // 2x larger = bonus
                if (size_ratio < 0.5f) attack_str *= 0.85f;  // 2x smaller = penalty
                
                // === COMBAT RESOLUTION ===
                float attack_chance = attack_str / (attack_str + defend_str + 0.1f);
                
                // Very high combat rate for extremely dynamic borders
                if (rand_float() < attack_chance * 1.5f) {
                    // Attacker wins - record result
                    if (result_count >= result_capacity) {
                        result_capacity *= 2;
                        CombatResult* new_results = (CombatResult*)realloc(results, result_capacity * sizeof(CombatResult));
                        if (!new_results) { free(results); return; }
                        results = new_results;
                    }
                    results[result_count++] = (CombatResult){nx, ny, cell->colony_id, neighbor->colony_id, attack_str};
                    
                    // Update success history for learning
                    if (d < 8) {
                        attacker->success_history[d] = utils_clamp_f(
                            attacker->success_history[d] + 0.05f * attacker->genome.learning_rate, 0.0f, 1.0f);
                    }
                } else if (rand_float() < 0.3f) {
                    // Defender successfully defends - slight penalty to attacker's direction
                    if (d < 8) {
                        attacker->success_history[d] = utils_clamp_f(
                            attacker->success_history[d] - 0.02f * attacker->genome.learning_rate, 0.0f, 1.0f);
                    }
                }
            }
        }
    }
    
    // Apply combat results
    for (int i = 0; i < result_count; i++) {
        Cell* cell = world_get_cell(world, results[i].x, results[i].y);
        if (cell && cell->colony_id == results[i].loser) {
            Colony* loser = world_get_colony(world, results[i].loser);
            Colony* winner = world_get_colony(world, results[i].winner);
            
            if (loser && loser->cell_count > 0) {
                loser->cell_count--;
                // Increase stress when losing cells
                loser->stress_level = utils_clamp_f(loser->stress_level + 0.01f, 0.0f, 1.0f);
            }
            
            // Cell is captured by winner
            if (winner && winner->active) {
                cell->colony_id = results[i].winner;
                cell->age = 0;
                cell->is_border = true;
                winner->cell_count++;
                // Reduce stress when winning
                winner->stress_level = utils_clamp_f(winner->stress_level - 0.005f, 0.0f, 1.0f);
            } else {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
            }
        }
    }
    
    free(results);
}

void simulation_tick(World* world) {
    if (!world) return;
    
    // Age all cells and handle starvation/toxin death
    for (int i = 0; i < world->width * world->height; i++) {
        Cell* cell = &world->cells[i];
        if (cell->colony_id == 0) continue;
        
        // Age the cell
        if (cell->age < 255) cell->age++;
        
        Colony* colony = world_get_colony(world, cell->colony_id);
        if (!colony || !colony->active) continue;
        
        // STARVATION: Cells in depleted areas may die
        float nutrients = world->nutrients[i];
        if (nutrients < 0.2f) {
            // Low nutrients - chance of cell death based on efficiency
            float death_chance = (0.2f - nutrients) * 0.1f * (1.0f - colony->genome.efficiency);
            if (rand_float() < death_chance) {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
                if (colony->cell_count > 0) colony->cell_count--;
                colony->stress_level = utils_clamp_f(colony->stress_level + 0.02f, 0.0f, 1.0f);
                continue;
            }
        }
        
        // TOXIN DEATH: Cells in toxic areas may die
        float toxins = world->toxins[i];
        if (toxins > 0.3f) {
            float death_chance = (toxins - 0.3f) * 0.15f * (1.0f - colony->genome.toxin_resistance);
            if (rand_float() < death_chance) {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
                if (colony->cell_count > 0) colony->cell_count--;
                colony->stress_level = utils_clamp_f(colony->stress_level + 0.02f, 0.0f, 1.0f);
                continue;
            }
        }
        
        // NATURAL DECAY: All cells have a small baseline death rate
        // This ensures colonies shrink over time if they're not actively growing
        // Border cells die faster (exposed), interior cells are more stable
        float base_death_rate = 0.015f;  // ~1.5% per tick baseline (high for visible churn)
        if (cell->is_border) {
            base_death_rate = 0.035f;  // Border cells much more vulnerable
        }
        
        // SIZE-BASED DECAY: Larger colonies decay faster
        // Resources must travel from border to interior - harder for big colonies
        // Think of resource transport from border to centroid
        float size_factor = 1.0f;
        if (colony->cell_count > 50) {
            // Each 100 cells adds 20% more decay (resources harder to distribute)
            size_factor = 1.0f + (float)(colony->cell_count - 50) / 500.0f;
        }
        base_death_rate *= size_factor;
        
        // INTERIOR STARVATION: Interior cells far from border decay faster
        // They're last to receive nutrients from the edge
        if (!cell->is_border && colony->cell_count > 100) {
            // For large colonies, interior cells struggle
            base_death_rate *= 1.3f;
        }
        
        // Biofilm protects against natural decay
        base_death_rate *= (1.0f - colony->biofilm_strength * 0.5f);
        // Efficiency reduces decay (better resource management)
        base_death_rate *= (1.0f - colony->genome.efficiency * 0.4f);
        
        if (rand_float() < base_death_rate) {
            cell->colony_id = 0;
            cell->age = 0;
            cell->is_border = false;
            if (colony->cell_count > 0) colony->cell_count--;
            continue;
        }
        
        // OLD AGE: Cells have limited lifespan
        if (cell->age > 120) {
            float age_death_chance = (cell->age - 120) * 0.001f;
            if (rand_float() < age_death_chance) {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
                if (colony->cell_count > 0) colony->cell_count--;
            }
        }
    }
    
    // Update environmental layers
    simulation_update_nutrients(world);
    
    // ENVIRONMENTAL DISTURBANCES: Periodic nutrient/toxin fluctuations
    // Every ~20 ticks, create environmental events
    if (world->tick % 20 == 0) {
        // 50% chance: nutrient shift
        if (rand_float() < 0.5f) {
            int cx = rand() % world->width;
            int cy = rand() % world->height;
            int radius = 10 + rand() % 20;
            for (int y = cy - radius; y <= cy + radius; y++) {
                for (int x = cx - radius; x <= cx + radius; x++) {
                    if (x >= 0 && x < world->width && y >= 0 && y < world->height) {
                        int dist2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                        if (dist2 <= radius * radius) {
                            int idx = y * world->width + x;
                            // Random nutrient change
                            world->nutrients[idx] = utils_clamp_f(
                                world->nutrients[idx] + (rand_float() - 0.5f) * 0.4f, 0.0f, 1.0f);
                        }
                    }
                }
            }
        }
    }
    
    // Run simulation phases
    simulation_spread(world);
    simulation_mutate(world);
    simulation_check_divisions(world);
    simulation_check_recombinations(world);
    
    // Combat resolution for more dynamic battles
    simulation_resolve_combat(world);
    
    // Count active colonies and total cells
    int active_colonies = 0;
    int total_cells = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) {
            active_colonies++;
            total_cells += world->colonies[i].cell_count;
        }
    }
    
    // Spontaneous generation: constant influx of new colonies
    // Creates competition and prevents any colony from dominating forever
    int world_size = world->width * world->height;
    float empty_ratio = 1.0f - (float)total_cells / (float)world_size;
    // Base 3% spawn chance + more when empty space available
    float spawn_chance = 0.03f + empty_ratio * 0.1f;
    if (active_colonies < 200 && rand_float() < spawn_chance) {
        // Find a random empty spot
        for (int attempts = 0; attempts < 30; attempts++) {
            int x = rand() % world->width;
            int y = rand() % world->height;
            Cell* cell = world_get_cell(world, x, y);
            if (cell && cell->colony_id == 0) {
                // Spawn a new random colony here
                Colony colony;
                memset(&colony, 0, sizeof(Colony));
                colony.genome = genome_create_random();
                colony.color = colony.genome.body_color;
                colony.cell_count = 1;
                colony.max_cell_count = 1;
                colony.active = true;
                colony.parent_id = 0;
                colony.shape_seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
                colony.wobble_phase = (float)(rand() % 628) / 100.0f;
                generate_scientific_name(colony.name, sizeof(colony.name));
                
                uint32_t id = world_add_colony(world, colony);
                if (id > 0) {
                    cell->colony_id = id;
                    cell->age = 0;
                }
                break;
            }
        }
    }
    
    // Recount all colony cell counts from grid to fix any inconsistencies
    simulation_update_colony_stats(world);
    
    // Update colony stats, state, and strategy
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
        
        // === STRATEGIC STATE UPDATES ===
        
        // Stress decay over time (colonies recover)
        colony->stress_level = utils_clamp_f(colony->stress_level - 0.002f, 0.0f, 1.0f);
        
        // Biofilm builds up based on investment trait
        if (colony->genome.biofilm_investment > 0.3f) {
            float target_biofilm = colony->genome.biofilm_investment * colony->genome.biofilm_tendency;
            if (colony->biofilm_strength < target_biofilm) {
                colony->biofilm_strength = utils_clamp_f(colony->biofilm_strength + 0.01f, 0.0f, 1.0f);
            }
        }
        // Biofilm decays slightly without investment
        colony->biofilm_strength = utils_clamp_f(colony->biofilm_strength - 0.002f, 0.0f, 1.0f);
        
        // Success history decay (fade old learnings)
        for (int d = 0; d < 8; d++) {
            colony->success_history[d] *= (0.995f + colony->genome.memory_factor * 0.004f);
        }
        
        // Update colony state based on stress
        if (colony->stress_level > colony->genome.sporulation_threshold && colony->genome.dormancy_threshold > 0.3f) {
            colony->state = COLONY_STATE_DORMANT;
            colony->is_dormant = true;
        } else if (colony->stress_level > 0.5f) {
            colony->state = COLONY_STATE_STRESSED;
            colony->is_dormant = false;
        } else {
            colony->state = COLONY_STATE_NORMAL;
            colony->is_dormant = false;
        }
        
        // Track population changes for learning
        int pop_delta = (int)colony->cell_count - (int)colony->last_population;
        colony->last_population = (uint32_t)colony->cell_count;
        
        // If growing, reinforce current strategy; if shrinking, consider changing
        if (pop_delta < -3 && colony->genome.learning_rate > 0.3f) {
            // Losing cells - slightly randomize success history to try new strategies
            int random_dir = rand() % 8;
            colony->success_history[random_dir] = utils_clamp_f(
                colony->success_history[random_dir] + 0.1f * rand_float(), 0.0f, 1.0f);
        }
        
        // Animate wobble phase for organic movement (legacy, kept for compatibility)
        colony->wobble_phase += 0.03f;
        if (colony->wobble_phase > 6.28318f) colony->wobble_phase -= 6.28318f;
        
        // Gradually evolve shape over time (slow, continuous morphing)
        colony->shape_evolution += 0.002f;
        if (colony->shape_evolution > 100.0f) colony->shape_evolution -= 100.0f;
    }
    
    world->tick++;
}
