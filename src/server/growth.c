#include "growth.h"
#include "genetics.h"
#include "world.h"
#include "../shared/simulation_common.h"
#include "../shared/utils.h"
#include "../shared/names.h"
#include "../shared/colors.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    int* data;
    int top;
    int capacity;
} Stack;

static Stack* stack_create(int capacity) {
    Stack* s = (Stack*)malloc(sizeof(Stack));
    if (!s) return NULL;
    s->data = (int*)malloc(capacity * 2 * sizeof(int));
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
        
        for (int d = 0; d < 8; d++) {
            int nx = x + DX8[d];
            int ny = y + DY8[d];
            
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
    
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == colony_id) {
            world->cells[i].component_id = -1;
        }
    }
    
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
                if (count >= MAX_COMPONENTS) {
                    *num_components = count;
                    return sizes;
                }
                if (count >= capacity) {
                    int new_capacity = capacity * 2;
                    if (new_capacity > MAX_COMPONENTS) new_capacity = MAX_COMPONENTS;
                    int* new_sizes = (int*)realloc(sizes, new_capacity * sizeof(int));
                    if (!new_sizes) {
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

void simulation_spread(World* world) {
    if (!world) return;
    
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
            
            for (int d = 0; d < 8; d++) {
                int nx = x + DX8[d];
                int ny = y + DY8[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor) continue;
                
                if (neighbor->colony_id == 0) {
                    float env_modifier = calculate_env_spread_modifier(world, colony, nx, ny, x, y);
                    float dir_weight = get_direction_weight(&colony->genome, DX8[d], DY8[d]);
                    float scent_modifier = get_scent_influence(world, x, y, DX8[d], DY8[d], 
                                                                cell->colony_id, &colony->genome);
                    float biomass_pressure = calculate_biomass_pressure(world, x, y, cell->colony_id);
                    
                    int enemy_count = count_enemy_neighbors(world, nx, ny, cell->colony_id);
                    float strategic_modifier = 1.0f;
                    if (enemy_count > 0) {
                        strategic_modifier *= (0.3f + colony->genome.aggression * 0.4f);
                    }
                    
                    float history_bonus = 1.0f + colony->success_history[d] * 0.3f;
                    float quorum_activation = get_quorum_activation(colony);
                    float quorum_boost = 1.0f + quorum_activation * colony->genome.motility * 0.8f;
                    float dormancy_factor = colony->is_dormant
                        ? (0.12f + colony->genome.dormancy_resistance * 0.28f)
                        : 1.0f;
                    
                    float curvature = calculate_curvature_boost(world, nx, ny, cell->colony_id);
                    float iso_correction = DIR8_WEIGHT[d];
                    float noise = 0.6f + rand_float() * 0.8f;
                    
                    float perception = calculate_perception_modifier(world, x, y, 
                                                                      DX8[d], DY8[d], colony);
                    
                    float spread_prob = colony->genome.spread_rate * colony->genome.metabolism * 
                                        env_modifier * dir_weight * scent_modifier * 
                                        strategic_modifier * history_bonus * biomass_pressure *
                                        quorum_boost * dormancy_factor * curvature *
                                        iso_correction * noise * perception * 3.0f;
                    
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
    
    for (int i = 0; i < pending_count; i++) {
        Cell* cell = world_get_cell(world, pending[i].x, pending[i].y);
        if (cell) {
            uint32_t old_colony = cell->colony_id;
            
            if (old_colony != 0) {
                Colony* old = world_get_colony(world, old_colony);
                if (old && old->cell_count > 0) {
                    old->cell_count--;
                    int cell_idx = pending[i].y * world->width + pending[i].x;
                    world_colony_remove_cell(world, old_colony, cell_idx);
                }
            }
            
            cell->colony_id = pending[i].colony_id;
            cell->age = 0;
            
            Colony* colony = world_get_colony(world, pending[i].colony_id);
            if (colony) {
                colony->cell_count++;
                
                int cell_idx = pending[i].y * world->width + pending[i].x;
                world_colony_add_cell(world, pending[i].colony_id, cell_idx);
                
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
    if (!world) return;
    
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        
        float baseline_rate = 0.08f + colony->genome.mutation_rate * 0.6f;
        baseline_rate *= (1.0f + colony->stress_level * 2.5f);
        baseline_rate *= (1.0f + (float)colony->cell_count / 300.0f);
        
        if (rand_float() < baseline_rate) {
            Genome original = colony->genome;
            
            genome_mutate(&colony->genome);
            colony->color = colony->genome.body_color;
            
            float genetic_distance = genome_distance(&original, &colony->genome);
            float speciation_chance = 0.02f + genetic_distance * 0.15f;
            
            if (colony->cell_count < 20) {
                speciation_chance = 0.0f;
            }
            
            if (rand_float() < speciation_chance) {
                int seed_x = -1, seed_y = -1;
                int attempts = 0;
                while (seed_x < 0 && attempts < 100) {
                    int j = rand() % (world->width * world->height);
                    Cell* cell = &world->cells[j];
                    if (cell->colony_id == colony->id && cell->is_border) {
                        seed_x = j % world->width;
                        seed_y = j / world->width;
                    }
                    attempts++;
                }
                
                if (seed_x >= 0) {
                    int cells_for_new = (int)(colony->cell_count * (0.05f + rand_float() * 0.1f));
                    cells_for_new = (cells_for_new < 3) ? 3 : (cells_for_new > 20) ? 20 : cells_for_new;
                    
                    Colony new_species;
                    memset(&new_species, 0, sizeof(Colony));
                    new_species.genome = colony->genome;
                    new_species.color = new_species.genome.body_color;
                    new_species.active = true;
                    new_species.parent_id = colony->id;
                    new_species.shape_seed = colony->shape_seed ^ (uint32_t)(rand() << 8);
                    new_species.wobble_phase = rand_float() * 6.28f;
                    new_species.cell_count = 0;
                    new_species.max_cell_count = 0;
                    generate_scientific_name(new_species.name, sizeof(new_species.name));
                    
                    colony->genome = original;
                    colony->color = original.body_color;
                    
                    uint32_t new_id = world_add_colony(world, new_species);
                    if (new_id > 0) {
                        int transferred = 0;
                        int queue_capacity = 1024;
                        int* queue = (int*)malloc(queue_capacity * sizeof(int));
                        if (!queue) continue;
                        int q_front = 0, q_back = 0;
                        queue[q_back++] = seed_y * world->width + seed_x;
                        
                        while (q_front < q_back && transferred < cells_for_new) {
                            int idx = queue[q_front++];
                            Cell* cell = &world->cells[idx];
                            if (cell->colony_id != colony->id) continue;
                            
                            cell->colony_id = new_id;
                            cell->age = 0;
                            transferred++;
                            if (colony->cell_count > 0) colony->cell_count--;
                            
                            world_colony_add_cell(world, new_id, idx);
                            
                            int cx = idx % world->width;
                            int cy = idx / world->width;
                            int dx[] = {-1, 1, 0, 0};
                            int dy[] = {0, 0, -1, 1};
                            for (int d = 0; d < 4; d++) {
                                int nx = cx + dx[d];
                                int ny = cy + dy[d];
                                if (nx >= 0 && nx < world->width && ny >= 0 && ny < world->height) {
                                    int nidx = ny * world->width + nx;
                                    if (world->cells[nidx].colony_id == colony->id) {
                                        if (q_back >= queue_capacity) {
                                            queue_capacity *= 2;
                                            int* new_queue = (int*)realloc(queue, queue_capacity * sizeof(int));
                                            if (!new_queue) break;
                                            queue = new_queue;
                                        }
                                        queue[q_back++] = nidx;
                                    }
                                }
                            }
                        }
                        free(queue);
                        
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
    
    bool division_occurred = false;
    
    for (size_t i = 0; i < world->colony_count && !division_occurred; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active || colony->cell_count < 2) continue;
        
        int num_components;
        int* sizes = find_connected_components(world, colony->id, &num_components);
        
        if (sizes && num_components > 1) {
            int largest_idx = 0;
            int largest_size = sizes[0];
            for (int c = 1; c < num_components; c++) {
                if (sizes[c] > largest_size) {
                    largest_size = sizes[c];
                    largest_idx = c;
                }
            }
            
            int orphaned_cells = 0;
            
            for (int c = 0; c < num_components; c++) {
                if (c == largest_idx) continue;
                if (sizes[c] < 5) {
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
                
                new_colony.shape_seed = colony->shape_seed ^ (uint32_t)rand() ^ ((uint32_t)rand() << 8);
                new_colony.wobble_phase = (float)(rand() % 628) / 100.0f;
                
                uint32_t new_id = world_add_colony(world, new_colony);
                
                for (int j = 0; j < world->width * world->height; j++) {
                    if (world->cells[j].colony_id == colony->id && 
                        world->cells[j].component_id == c) {
                        world->cells[j].colony_id = new_id;
                    }
                }
            }
            
            colony->cell_count = (size_t)largest_size;
            division_occurred = true;
        }
        
        free(sizes);
    }
}

void simulation_check_recombinations(World* world) {
    if (!world) return;
    
    static const int RDX[] = {0, 1, 0, -1};
    static const int RDY[] = {-1, 0, 1, 0};
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony_a = world_get_colony(world, cell->colony_id);
            if (!colony_a) continue;
            
            for (int d = 1; d <= 2; d++) {
                int nx = x + RDX[d];
                int ny = y + RDY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor || neighbor->colony_id == 0 || neighbor->colony_id == cell->colony_id) continue;
                
                Colony* colony_b = world_get_colony(world, neighbor->colony_id);
                if (!colony_b) continue;
                
                if (colony_a->parent_id != colony_b->id && colony_b->parent_id != colony_a->id) {
                    if (colony_a->parent_id == 0 || colony_a->parent_id != colony_b->parent_id) {
                        continue;
                    }
                }
                
                float distance = genome_distance(&colony_a->genome, &colony_b->genome);
                
                float threshold = 0.05f;
                
                float avg_affinity = (colony_a->genome.merge_affinity + colony_b->genome.merge_affinity) / 2.0f;
                threshold += avg_affinity * 0.1f;
                
                if (distance <= threshold) {
                    Colony* larger = colony_a->cell_count >= colony_b->cell_count ? colony_a : colony_b;
                    Colony* smaller = colony_a->cell_count >= colony_b->cell_count ? colony_b : colony_a;
                    
                    larger->genome = genome_merge(&larger->genome, larger->cell_count,
                                                  &smaller->genome, smaller->cell_count);
                    
                    for (int j = 0; j < world->width * world->height; j++) {
                        if (world->cells[j].colony_id == smaller->id) {
                            world->cells[j].colony_id = larger->id;
                        }
                    }
                    
                    larger->cell_count += smaller->cell_count;
                    smaller->cell_count = 0;
                    smaller->active = false;
                    
                    return;
                }
            }
        }
    }
}
