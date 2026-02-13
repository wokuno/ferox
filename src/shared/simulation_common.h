#ifndef FEROX_SIMULATION_COMMON_H
#define FEROX_SIMULATION_COMMON_H

#include "../shared/types.h"

#define NUTRIENT_DEPLETION_RATE 0.05f
#define NUTRIENT_REGEN_RATE 0.002f
#define TOXIN_DECAY_RATE 0.01f
#define QUORUM_SENSING_RADIUS 3

extern const int DX8[8];
extern const int DY8[8];
extern const float DIR8_WEIGHT[8];

float calculate_local_density(World* world, int x, int y, uint32_t colony_id);
float calculate_env_spread_modifier(World* world, Colony* colony, int tx, int ty, int sx, int sy);
float calculate_biomass_pressure(World* world, int x, int y, uint32_t colony_id);
float get_quorum_activation(const Colony* colony);
float get_direction_weight(Genome* g, int dx, int dy);
float calculate_curvature_boost(World* world, int tx, int ty, uint32_t colony_id);
float calculate_perception_modifier(World* world, int x, int y, int dx, int dy, Colony* colony);
float get_scent_influence(World* world, int x, int y, int dx, int dy, uint32_t colony_id, const Genome* genome);
int count_friendly_neighbors(World* world, int x, int y, uint32_t colony_id);
int count_enemy_neighbors(World* world, int x, int y, uint32_t colony_id);

#endif
