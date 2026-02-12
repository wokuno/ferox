#ifndef FEROX_WORLD_H
#define FEROX_WORLD_H

#include "../shared/types.h"

// Create a new world with given dimensions
World* world_create(int width, int height);

// Destroy world and free all resources
void world_destroy(World* world);

// Initialize world with random colonies at random positions
void world_init_random_colonies(World* world, int count);

// Get cell at coordinates (returns NULL if out of bounds)
Cell* world_get_cell(World* world, int x, int y);

// Get colony by ID (returns NULL if not found)
Colony* world_get_colony(World* world, uint32_t id);

// Add a colony to the world, returns assigned ID
uint32_t world_add_colony(World* world, Colony colony);

// Remove a colony from the world
void world_remove_colony(World* world, uint32_t id);

#endif // FEROX_WORLD_H
