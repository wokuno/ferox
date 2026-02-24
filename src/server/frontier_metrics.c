#include "frontier_metrics.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692

typedef struct {
    uint32_t lineage_id;
    size_t count;
} LineageCount;

static const Colony* find_colony_any(const World* world, uint32_t id) {
    if (!world || id == 0) {
        return NULL;
    }

    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].id == id) {
            return &world->colonies[i];
        }
    }
    return NULL;
}

static uint32_t resolve_root_lineage_id(const World* world, uint32_t colony_id) {
    if (!world || colony_id == 0) {
        return 0;
    }

    uint32_t current = colony_id;
    size_t guard = world->colony_count + 1;

    while (guard-- > 0) {
        const Colony* colony = find_colony_any(world, current);
        if (!colony || colony->parent_id == 0) {
            return current;
        }
        current = colony->parent_id;
    }

    return colony_id;
}

static void increment_lineage_count(LineageCount* counts, size_t* used, uint32_t lineage_id) {
    for (size_t i = 0; i < *used; i++) {
        if (counts[i].lineage_id == lineage_id) {
            counts[i].count++;
            return;
        }
    }

    counts[*used].lineage_id = lineage_id;
    counts[*used].count = 1;
    (*used)++;
}

static bool is_frontier_cell(const World* world, int x, int y, uint32_t colony_id) {
    static const int dx4[4] = {0, 1, 0, -1};
    static const int dy4[4] = {-1, 0, 1, 0};

    for (int i = 0; i < 4; i++) {
        int nx = x + dx4[i];
        int ny = y + dy4[i];
        if (nx < 0 || nx >= world->width || ny < 0 || ny >= world->height) {
            continue;
        }

        uint32_t neighbor = world->cells[ny * world->width + nx].colony_id;
        if (neighbor != colony_id) {
            return true;
        }
    }

    return false;
}

bool frontier_telemetry_compute(const World* world, uint32_t seed, FrontierTelemetry* out) {
    if (!world || !out || world->width <= 0 || world->height <= 0) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->seed = seed;
    out->tick = world->tick;

    size_t lineage_capacity = world->colony_count > 0 ? world->colony_count : 1;
    LineageCount* total_counts = (LineageCount*)calloc(lineage_capacity, sizeof(LineageCount));
    LineageCount* frontier_counts = (LineageCount*)calloc(lineage_capacity, sizeof(LineageCount));
    if (!total_counts || !frontier_counts) {
        free(total_counts);
        free(frontier_counts);
        return false;
    }

    size_t total_used = 0;
    size_t frontier_used = 0;
    double sum_x = 0.0;
    double sum_y = 0.0;

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            uint32_t colony_id = world->cells[y * world->width + x].colony_id;
            if (colony_id == 0) {
                continue;
            }

            uint32_t lineage_id = resolve_root_lineage_id(world, colony_id);
            increment_lineage_count(total_counts, &total_used, lineage_id);

            out->occupied_cells++;
            sum_x += (double)x;
            sum_y += (double)y;
        }
    }

    out->active_lineages = total_used;

    bool sector_seen[FRONTIER_TELEMETRY_SECTORS] = {false};
    double center_x = out->occupied_cells > 0 ? (sum_x / (double)out->occupied_cells) : 0.0;
    double center_y = out->occupied_cells > 0 ? (sum_y / (double)out->occupied_cells) : 0.0;
    const double sector_size = TWO_PI / (double)FRONTIER_TELEMETRY_SECTORS;

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            uint32_t colony_id = world->cells[y * world->width + x].colony_id;
            if (colony_id == 0 || !is_frontier_cell(world, x, y, colony_id)) {
                continue;
            }

            out->frontier_cells++;
            uint32_t lineage_id = resolve_root_lineage_id(world, colony_id);
            increment_lineage_count(frontier_counts, &frontier_used, lineage_id);

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
            double p = (double)frontier_counts[i].count * inv;
            concentration += p * p;
        }
        out->lineage_diversity_proxy = (float)(1.0 - concentration);
    }

    if (out->occupied_cells > 0) {
        double inv = 1.0 / (double)out->occupied_cells;
        double entropy = 0.0;
        for (size_t i = 0; i < total_used; i++) {
            double p = (double)total_counts[i].count * inv;
            if (p > 0.0) {
                entropy -= p * (log(p) / log(2.0));
            }
        }
        out->lineage_entropy_bits = (float)entropy;
    }

    free(total_counts);
    free(frontier_counts);
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
