#ifndef FEROX_GENETICS_H
#define FEROX_GENETICS_H

#include "../shared/types.h"

// Create random genome with valid ranges (all values 0.0-1.0)
Genome genome_create_random(void);

// Mutate a genome slightly based on its mutation_rate
void genome_mutate(Genome* genome);

// Calculate genetic distance between two genomes (0-1 scale)
float genome_distance(const Genome* a, const Genome* b);

// Merge two genomes (weighted average based on cell counts)
Genome genome_merge(const Genome* a, size_t count_a, const Genome* b, size_t count_b);

// Check if two genomes are compatible for recombination
bool genome_compatible(const Genome* a, const Genome* b, float threshold);

#endif // FEROX_GENETICS_H
