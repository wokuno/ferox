#ifndef FEROX_NAMES_H
#define FEROX_NAMES_H

#include <stddef.h>

// Generate a random scientific name in "Genus species" format
// Buffer must be at least 64 bytes
void generate_scientific_name(char *buffer, size_t buffer_size);

#endif // FEROX_NAMES_H
