#include "world.h"
#include "genetics.h"
#include "../shared/utils.h"
#include "../shared/names.h"
#include "../shared/colors.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define INITIAL_COLONY_CAPACITY 16

static int world_ensure_colony_index_capacity(World* world, uint32_t id) {
    if (!world || id == 0) {
        return -1;
    }

    if ((size_t)id < world->colony_index_capacity) {
        return 0;
    }

    size_t new_capacity = world->colony_index_capacity ? world->colony_index_capacity : 64;
    while (new_capacity <= (size_t)id) {
        if (new_capacity > (SIZE_MAX / 2)) {
            return -1;
        }
        new_capacity *= 2;
    }

    uint32_t* new_map = (uint32_t*)realloc(world->colony_index_map, new_capacity * sizeof(uint32_t));
    if (!new_map) {
        return -1;
    }

    for (size_t i = world->colony_index_capacity; i < new_capacity; i++) {
        new_map[i] = UINT32_MAX;
    }

    world->colony_index_map = new_map;
    world->colony_index_capacity = new_capacity;
    return 0;
}

World* world_create(int width, int height) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }
    
    World* world = (World*)malloc(sizeof(World));
    if (!world) return NULL;
    
    world->width = width;
    world->height = height;
    world->tick = 0;
    world->next_colony_id = 1;
    world->colony_count = 0;
    world->colony_capacity = INITIAL_COLONY_CAPACITY;
    world->colony_index_map = NULL;
    world->colony_index_capacity = 0;
    
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
    
    if (!world->nutrients || !world->toxins || !world->signals || !world->signal_source ||
        !world->alarm_signals || !world->alarm_source) {
        if (world->nutrients) free(world->nutrients);
        if (world->toxins) free(world->toxins);
        if (world->signals) free(world->signals);
        if (world->alarm_signals) free(world->alarm_signals);
        if (world->signal_source) free(world->signal_source);
        if (world->alarm_source) free(world->alarm_source);
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

    if (world_ensure_colony_index_capacity(world, 64) != 0) {
        free(world->colonies);
        free(world->nutrients);
        free(world->toxins);
        free(world->signals);
        free(world->alarm_signals);
        free(world->signal_source);
        free(world->alarm_source);
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
    if (world->colony_index_map) free(world->colony_index_map);
    if (world->nutrients) free(world->nutrients);
    if (world->toxins) free(world->toxins);
    if (world->signals) free(world->signals);
    if (world->alarm_signals) free(world->alarm_signals);
    if (world->signal_source) free(world->signal_source);
    if (world->alarm_source) free(world->alarm_source);
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

    if ((size_t)id < world->colony_index_capacity) {
        uint32_t idx = world->colony_index_map[id];
        if (idx != UINT32_MAX && idx < world->colony_count) {
            Colony* colony = &world->colonies[idx];
            if (colony->id == id) {
                return colony->active ? colony : NULL;
            }
        }
    }

    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].id == id && world->colonies[i].active) {
            if ((size_t)id < world->colony_index_capacity) {
                world->colony_index_map[id] = (uint32_t)i;
            }
            return &world->colonies[i];
        }
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
        world->colonies = new_colonies;
        world->colony_capacity = new_capacity;
    }
    
    // Assign new ID (per-world incrementing, start from 1)
    colony.id = world->next_colony_id++;
    colony.active = true;

    if (world_ensure_colony_index_capacity(world, colony.id) != 0) {
        return 0;
    }

    world->colonies[world->colony_count++] = colony;
    world->colony_index_map[colony.id] = (uint32_t)(world->colony_count - 1);
    return colony.id;
}

void world_remove_colony(World* world, uint32_t id) {
    if (!world || id == 0) return;
    
    // Mark colony as inactive
    Colony* colony = world_get_colony(world, id);
    if (colony) {
        colony->active = false;
    }

    if ((size_t)id < world->colony_index_capacity) {
        world->colony_index_map[id] = UINT32_MAX;
    }
    
    // Clear all cells belonging to this colony
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == id) {
            world->cells[i].colony_id = 0;
            world->cells[i].age = 0;
        }
    }
}
