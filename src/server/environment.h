#ifndef FEROX_ENVIRONMENT_H
#define FEROX_ENVIRONMENT_H

#include "world.h"

void simulation_update_nutrients(World* world);
void simulation_decay_toxins(World* world);
void simulation_produce_toxins(World* world);
void simulation_apply_toxin_damage(World* world);
void simulation_consume_resources(World* world);
void simulation_update_scents(World* world);

#endif
