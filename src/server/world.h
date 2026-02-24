#ifndef FEROX_WORLD_H
#define FEROX_WORLD_H

#include "../shared/types.h"

typedef struct {
    bool enabled;
    float half_saturation;
    float uptake_min;
    float uptake_max;
    float growth_coupling;
} MonodKineticsConfig;

#define RD_FIELD_MAX_DIFFUSION 0.25f

#define RD_DEFAULT_NUTRIENT_DIFFUSION 0.00f
#define RD_DEFAULT_NUTRIENT_DECAY 0.00f
#define RD_DEFAULT_TOXIN_DIFFUSION 0.00f
#define RD_DEFAULT_TOXIN_DECAY 0.05f
#define RD_DEFAULT_SIGNAL_DIFFUSION 0.075f
#define RD_DEFAULT_SIGNAL_DECAY 0.10f

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

// Configure optional Monod-style nutrient uptake and growth coupling.
void world_set_monod_kinetics(World* world, const MonodKineticsConfig* config);

// Retrieve current Monod-style kinetics configuration.
MonodKineticsConfig world_get_monod_kinetics(const World* world);

// Track cell addition for O(active_cells) removal and centroid
void world_colony_add_cell(World* world, uint32_t colony_id, uint32_t cell_idx);
void world_colony_remove_cell(World* world, uint32_t colony_id, uint32_t cell_idx);

// Validate and apply reaction-diffusion controls.
bool world_set_rd_controls(World* world, const RDSolverControls* controls,
                           char* err_buf, size_t err_buf_size);

// Copy current solver controls.
RDSolverControls world_get_rd_controls(const World* world);

// Configure horizontal gene transfer kinetics.
void world_set_hgt_kinetics(World* world, const HGTKinetics* kinetics);

// Restore default horizontal gene transfer kinetics.
void world_reset_hgt_kinetics(World* world);

// Reset accumulated horizontal gene transfer metrics.
void world_reset_hgt_metrics(World* world);

#endif // FEROX_WORLD_H
