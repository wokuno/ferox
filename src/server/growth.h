#ifndef FEROX_GROWTH_H
#define FEROX_GROWTH_H

#include "world.h"

void simulation_spread(World* world);
void simulation_mutate(World* world);
void simulation_check_divisions(World* world);
void simulation_check_recombinations(World* world);

#endif
