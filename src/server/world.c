#include "world.h"
#include "genetics.h"
#include "../shared/utils.h"
#include "../shared/names.h"
#include "../shared/colors.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_COLONY_CAPACITY 16
#define INITIAL_LOOKUP_CAPACITY 32

World* world_create(int width, int height) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }
    
    World* world = (World*)malloc(sizeof(World));
    if (!world) return NULL;
    
    world->width = width;
    world->height = height;
    world->tick = 0;
    world->colony_count = 0;
    world->colony_capacity = INITIAL_COLONY_CAPACITY;
    
    size_t grid_size = (size_t)(width * height);
    
    // Allocate cells as flat array
    world->cells = (Cell*)calloc(grid_size, sizeof(Cell));
    if (!world->cells) {
        free(world);
        return NULL;
    }
    
    // Initialize all cells
    for (int i = 0; i < width * height; i++) {
        world->cells[i].colony_id = 0;
        world->cells[i].is_border = false;
        world->cells[i].age = 0;
        world->cells[i].component_id = -1;
    }
    
    // Allocate environmental layers
    world->nutrients = (float*)malloc(grid_size * sizeof(float));
    world->toxins = (float*)calloc(grid_size, sizeof(float));  // Start with no toxins
    world->signals = (float*)calloc(grid_size, sizeof(float)); // Start with no signals
    world->alarm_signals = (float*)calloc(grid_size, sizeof(float)); // Start with no alarms
    world->signal_source = (uint32_t*)calloc(grid_size, sizeof(uint32_t));
    world->alarm_source = (uint32_t*)calloc(grid_size, sizeof(uint32_t));
    world->scratch_signals = (float*)calloc(grid_size, sizeof(float));
    world->scratch_sources = (uint32_t*)calloc(grid_size, sizeof(uint32_t));
    
    if (!world->nutrients || !world->toxins || !world->signals || !world->signal_source ||
        !world->alarm_signals || !world->alarm_source ||
        !world->scratch_signals || !world->scratch_sources) {
        if (world->nutrients) free(world->nutrients);
        if (world->toxins) free(world->toxins);
        if (world->signals) free(world->signals);
        if (world->alarm_signals) free(world->alarm_signals);
        if (world->signal_source) free(world->signal_source);
        if (world->alarm_source) free(world->alarm_source);
        if (world->scratch_signals) free(world->scratch_signals);
        if (world->scratch_sources) free(world->scratch_sources);
        free(world->cells);
        free(world);
        return NULL;
    }
    
    // Initialize nutrients with full resources
    for (size_t i = 0; i < grid_size; i++) {
        world->nutrients[i] = 1.0f;
    }
    
    // Allocate colony array
    world->colonies = (Colony*)malloc(world->colony_capacity * sizeof(Colony));
    if (!world->colonies) {
        free(world->nutrients);
        free(world->toxins);
        free(world->signals);
        free(world->signal_source);
        free(world->cells);
        free(world);
        return NULL;
    }
    
    // Allocate colony lookup table
    world->colony_by_id_capacity = INITIAL_LOOKUP_CAPACITY;
    world->colony_by_id = (Colony**)calloc(world->colony_by_id_capacity, sizeof(Colony*));
    if (!world->colony_by_id) {
        free(world->colonies);
        free(world->nutrients);
        free(world->toxins);
        free(world->signals);
        free(world->signal_source);
        free(world->cells);
        free(world);
        return NULL;
    }
    
    return world;
}

void world_destroy(World* world) {
    if (!world) return;
    
    if (world->cells) free(world->cells);
    if (world->colonies) free(world->colonies);
    if (world->colony_by_id) free(world->colony_by_id);
    if (world->nutrients) free(world->nutrients);
    if (world->toxins) free(world->toxins);
    if (world->signals) free(world->signals);
    if (world->alarm_signals) free(world->alarm_signals);
    if (world->signal_source) free(world->signal_source);
    if (world->alarm_source) free(world->alarm_source);
    if (world->scratch_signals) free(world->scratch_signals);
    if (world->scratch_sources) free(world->scratch_sources);
    free(world);
}

void world_init_random_colonies(World* world, int count) {
    if (!world || count <= 0) return;
    
    for (int i = 0; i < count; i++) {
        Colony colony;
        memset(&colony, 0, sizeof(Colony));
        colony.id = 0;  // Will be assigned by world_add_colony
        generate_scientific_name(colony.name, sizeof(colony.name));
        colony.genome = genome_create_random();
        colony.color = colony.genome.body_color;  // Use genome's body color
        colony.cell_count = 0;
        colony.max_cell_count = 0;
        colony.active = true;
        colony.age = 0;
        colony.parent_id = 0;
        
        // Generate unique shape seed for procedural shape generation
        colony.shape_seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
        colony.wobble_phase = (float)(rand() % 628) / 100.0f;
        
        uint32_t id = world_add_colony(world, colony);
        
        // Place colony at random position (retry if occupied)
        int x, y;
        int max_tries = 100;
        Cell* cell = NULL;
        for (int try = 0; try < max_tries; try++) {
            x = rand_range(0, world->width - 1);
            y = rand_range(0, world->height - 1);
            cell = world_get_cell(world, x, y);
            if (cell && cell->colony_id == 0) {
                break;
            }
            cell = NULL;
        }
        
        if (cell) {
            cell->colony_id = id;
            cell->age = 0;
            
            // Update cell count
            Colony* col = world_get_colony(world, id);
            if (col) {
                col->cell_count = 1;
                col->max_cell_count = 1;
            }
        } else {
            // Could not find empty cell - deactivate colony
            Colony* col = world_get_colony(world, id);
            if (col) {
                col->active = false;
            }
        }
    }
}

Cell* world_get_cell(World* world, int x, int y) {
    if (!world || x < 0 || x >= world->width || y < 0 || y >= world->height) {
        return NULL;
    }
    return &world->cells[y * world->width + x];
}

Colony* world_get_colony(World* world, uint32_t id) {
    if (!world || id == 0) return NULL;
    
    if (id < world->colony_by_id_capacity) {
        Colony* c = world->colony_by_id[id];
        if (c && c->active) return c;
    }
    return NULL;
}

uint32_t world_add_colony(World* world, Colony colony) {
    if (!world) return 0;
    
    // Expand array if needed
    if (world->colony_count >= world->colony_capacity) {
        size_t new_capacity = world->colony_capacity * 2;
        Colony* new_colonies = (Colony*)realloc(world->colonies, new_capacity * sizeof(Colony));
        if (!new_colonies) return 0;
        // Realloc may move the array; rebuild all lookup pointers
        if (new_colonies != world->colonies) {
            world->colonies = new_colonies;
            for (size_t i = 0; i < world->colony_count; i++) {
                uint32_t cid = world->colonies[i].id;
                if (cid < world->colony_by_id_capacity && world->colony_by_id[cid]) {
                    world->colony_by_id[cid] = &world->colonies[i];
                }
            }
        }
        world->colonies = new_colonies;
        world->colony_capacity = new_capacity;
    }
    
    // Assign new ID (simple incrementing, start from 1)
    static uint32_t next_id = 1;
    colony.id = next_id++;
    colony.active = true;
    
    world->colonies[world->colony_count] = colony;
    
    // Grow lookup table if needed
    if (colony.id >= world->colony_by_id_capacity) {
        size_t new_cap = world->colony_by_id_capacity;
        while (new_cap <= colony.id) new_cap *= 2;
        Colony** new_table = (Colony**)realloc(world->colony_by_id, new_cap * sizeof(Colony*));
        if (!new_table) return 0;
        memset(new_table + world->colony_by_id_capacity, 0,
               (new_cap - world->colony_by_id_capacity) * sizeof(Colony*));
        world->colony_by_id = new_table;
        world->colony_by_id_capacity = new_cap;
    }
    world->colony_by_id[colony.id] = &world->colonies[world->colony_count];
    
    world->colony_count++;
    return colony.id;
}

void world_remove_colony(World* world, uint32_t id) {
    if (!world || id == 0) return;
    
    // Mark colony as inactive
    Colony* colony = world_get_colony(world, id);
    if (colony) {
        colony->active = false;
        // Clear lookup table entry
        if (id < world->colony_by_id_capacity) {
            world->colony_by_id[id] = NULL;
        }
    }
    
    // Clear all cells belonging to this colony
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == id) {
            world->cells[i].colony_id = 0;
            world->cells[i].age = 0;
        }
    }
}
