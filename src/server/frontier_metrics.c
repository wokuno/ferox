#include "frontier_metrics.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692

typedef struct {
    uint32_t cell_idx;
} FrontierCellIndex;

static const Colony* find_colony_any(const World* world, uint32_t id) {
    if (!world || id == 0) {
        return NULL;
    }

    if (world->colony_by_id && id < world->colony_by_id_capacity) {
        Colony* colony = world->colony_by_id[id];
        if (colony) {
            return colony;
        }
    }

    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].id == id) {
            return &world->colonies[i];
        }
    }
    return NULL;
}

static uint32_t resolve_root_lineage_id_cached(const World* world,
                                               uint32_t colony_id,
                                               uint32_t* root_cache,
                                               size_t root_cache_capacity) {
    if (!world || colony_id == 0) {
        return 0;
    }

    if (root_cache && colony_id < root_cache_capacity && root_cache[colony_id] != 0) {
        return root_cache[colony_id];
    }

    uint32_t current = colony_id;
    size_t guard = world->colony_count + 1;

    while (guard-- > 0) {
        const Colony* colony = find_colony_any(world, current);
        if (!colony || colony->parent_id == 0) {
            if (root_cache && colony_id < root_cache_capacity) {
                root_cache[colony_id] = current;
            }
            return current;
        }
        current = colony->parent_id;
    }

    if (root_cache && colony_id < root_cache_capacity) {
        root_cache[colony_id] = colony_id;
    }
    return colony_id;
}

static void increment_lineage_count_direct(size_t* counts,
                                           uint32_t* active_ids,
                                           size_t* used,
                                           uint32_t lineage_id,
                                           size_t delta) {
    if (!counts || !active_ids || !used || lineage_id == 0) {
        return;
    }

    if (counts[lineage_id] == 0) {
        active_ids[*used] = lineage_id;
        (*used)++;
    }

    counts[lineage_id] += delta;
}

bool frontier_telemetry_compute(const World* world, uint32_t seed, FrontierTelemetry* out) {
    if (!world || !out || world->width <= 0 || world->height <= 0) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->seed = seed;
    out->tick = world->tick;

    size_t root_cache_capacity = world->colony_by_id_capacity > 0
        ? world->colony_by_id_capacity
        : (world->colony_count + 1);
    size_t* total_counts = (size_t*)calloc(root_cache_capacity, sizeof(size_t));
    size_t* frontier_counts = (size_t*)calloc(root_cache_capacity, sizeof(size_t));
    uint32_t* total_lineages = (uint32_t*)calloc(root_cache_capacity, sizeof(uint32_t));
    uint32_t* frontier_lineages = (uint32_t*)calloc(root_cache_capacity, sizeof(uint32_t));
    uint32_t* root_cache = (uint32_t*)calloc(root_cache_capacity, sizeof(uint32_t));
    FrontierCellIndex* frontier_cells = NULL;
    size_t frontier_capacity = 0;
    size_t frontier_used_cells = 0;
    if (!total_counts || !frontier_counts || !total_lineages || !frontier_lineages || !root_cache) {
        free(total_counts);
        free(frontier_counts);
        free(total_lineages);
        free(frontier_lineages);
        free(root_cache);
        return false;
    }

    size_t total_used = 0;
    size_t frontier_used = 0;
    double sum_x = 0.0;
    double sum_y = 0.0;

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            const Cell* cell = &world->cells[y * world->width + x];
            uint32_t colony_id = cell->colony_id;
            if (colony_id == 0) {
                continue;
            }

            uint32_t lineage_id = resolve_root_lineage_id_cached(
                world, colony_id, root_cache, root_cache_capacity);
            increment_lineage_count_direct(total_counts, total_lineages, &total_used, lineage_id, 1);

            out->occupied_cells++;
            sum_x += (double)x;
            sum_y += (double)y;

            // Simulation code maintains per-cell border state; reuse it here
            // instead of rescanning neighbors for telemetry.
            if (!cell->is_border) {
                continue;
            }

            if (frontier_used_cells == frontier_capacity) {
                size_t new_capacity = frontier_capacity == 0 ? 1024 : frontier_capacity * 2;
                FrontierCellIndex* new_frontier_cells = (FrontierCellIndex*)realloc(
                    frontier_cells, new_capacity * sizeof(FrontierCellIndex));
                if (!new_frontier_cells) {
                    free(total_counts);
                    free(frontier_counts);
                    free(root_cache);
                    free(frontier_cells);
                    return false;
                }
                frontier_cells = new_frontier_cells;
                frontier_capacity = new_capacity;
            }

            frontier_cells[frontier_used_cells++].cell_idx = (uint32_t)(y * world->width + x);
            out->frontier_cells++;
            increment_lineage_count_direct(frontier_counts, frontier_lineages, &frontier_used, lineage_id, 1);
        }
    }

    out->active_lineages = total_used;

    bool sector_seen[FRONTIER_TELEMETRY_SECTORS] = {false};
    double center_x = out->occupied_cells > 0 ? (sum_x / (double)out->occupied_cells) : 0.0;
    double center_y = out->occupied_cells > 0 ? (sum_y / (double)out->occupied_cells) : 0.0;
    const double sector_size = TWO_PI / (double)FRONTIER_TELEMETRY_SECTORS;

    for (size_t i = 0; i < frontier_used_cells; i++) {
        uint32_t idx = frontier_cells[i].cell_idx;
        int x = (int)(idx % (uint32_t)world->width);
        int y = (int)(idx / (uint32_t)world->width);
        double dx = (double)x - center_x;
        double dy = (double)y - center_y;
        double angle = atan2(dy, dx);
        if (angle < 0.0) {
            angle += TWO_PI;
        }

        int sector = (int)(angle / sector_size);
        if (sector < 0) {
            sector = 0;
        } else if (sector >= FRONTIER_TELEMETRY_SECTORS) {
            sector = FRONTIER_TELEMETRY_SECTORS - 1;
        }
        sector_seen[sector] = true;
    }

    for (int i = 0; i < FRONTIER_TELEMETRY_SECTORS; i++) {
        if (sector_seen[i]) {
            out->frontier_sector_count++;
        }
    }

    if (out->frontier_cells > 0) {
        double inv = 1.0 / (double)out->frontier_cells;
        double concentration = 0.0;
        for (size_t i = 0; i < frontier_used; i++) {
            uint32_t lineage_id = frontier_lineages[i];
            double p = (double)frontier_counts[lineage_id] * inv;
            concentration += p * p;
        }
        out->lineage_diversity_proxy = (float)(1.0 - concentration);
    }

    if (out->occupied_cells > 0) {
        double inv = 1.0 / (double)out->occupied_cells;
        double entropy = 0.0;
        for (size_t i = 0; i < total_used; i++) {
            uint32_t lineage_id = total_lineages[i];
            double p = (double)total_counts[lineage_id] * inv;
            if (p > 0.0) {
                entropy -= p * (log(p) / log(2.0));
            }
        }
        out->lineage_entropy_bits = (float)entropy;
    }

    free(total_counts);
    free(frontier_counts);
    free(total_lineages);
    free(frontier_lineages);
    free(root_cache);
    free(frontier_cells);
    return true;
}

int frontier_telemetry_format_logfmt(const FrontierTelemetry* sample, char* buffer, size_t buffer_size) {
    if (!sample || !buffer || buffer_size == 0) {
        return -1;
    }

    int written = snprintf(
        buffer,
        buffer_size,
        "seed=%u tick=%llu frontier_sector_count=%u lineage_diversity_proxy=%.6f lineage_entropy_bits=%.6f frontier_cells=%zu occupied_cells=%zu active_lineages=%zu",
        sample->seed,
        (unsigned long long)sample->tick,
        sample->frontier_sector_count,
        sample->lineage_diversity_proxy,
        sample->lineage_entropy_bits,
        sample->frontier_cells,
        sample->occupied_cells,
        sample->active_lineages);

    if (written < 0 || (size_t)written >= buffer_size) {
        return -1;
    }
    return written;
}
