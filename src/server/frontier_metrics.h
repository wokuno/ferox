#ifndef FEROX_FRONTIER_METRICS_H
#define FEROX_FRONTIER_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "world.h"

#define FRONTIER_TELEMETRY_SECTORS 16

typedef struct {
    uint32_t seed;
    uint64_t tick;
    uint32_t frontier_sector_count;
    float lineage_diversity_proxy;
    float lineage_entropy_bits;
    size_t frontier_cells;
    size_t occupied_cells;
    size_t active_lineages;
} FrontierTelemetry;

bool frontier_telemetry_compute(const World* world, uint32_t seed, FrontierTelemetry* out);
int frontier_telemetry_format_logfmt(const FrontierTelemetry* sample, char* buffer, size_t buffer_size);

#endif
