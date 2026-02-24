#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/shared/utils.h"
#include "../src/server/simulation.h"
#include "../src/server/world.h"

typedef struct {
    float occupied_ratio;
    float active_colonies;
    float dominant_share;
} SeedMetrics;

typedef struct {
    float mean;
    float stddev;
    float min;
    float max;
} Summary;

typedef struct {
    const char* name;
    float expected_mean;
    float mean_tolerance;
    float expected_stddev;
    float stddev_tolerance;
} MetricThreshold;

static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED\\n    %s\\n    At %s:%d\\n", msg, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

static Summary summarize(const float* values, int count) {
    Summary s = {0};
    if (count <= 0) {
        return s;
    }

    s.min = values[0];
    s.max = values[0];
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += values[i];
        if (values[i] < s.min) s.min = values[i];
        if (values[i] > s.max) s.max = values[i];
    }

    s.mean = (float)(sum / (double)count);

    double sq_sum = 0.0;
    for (int i = 0; i < count; i++) {
        double delta = (double)values[i] - (double)s.mean;
        sq_sum += delta * delta;
    }
    s.stddev = (float)sqrt(sq_sum / (double)count);
    return s;
}

static int count_grid_cells(const World* world, uint32_t colony_id) {
    int count = 0;
    const int cells = world->width * world->height;
    for (int i = 0; i < cells; i++) {
        if (world->cells[i].colony_id == colony_id) {
            count++;
        }
    }
    return count;
}

static SeedMetrics run_seed(uint32_t seed) {
    const int width = 96;
    const int height = 64;
    const int initial_colonies = 14;
    const int ticks = 90;

    SeedMetrics m = {0};

    World* world = world_create(width, height);
    if (!world) {
        return m;
    }

    rng_seed(seed);
    srand(seed);
    world_init_random_colonies(world, initial_colonies);

    for (int i = 0; i < ticks; i++) {
        simulation_tick(world);
    }

    int active_colonies = 0;
    int total_cells = 0;
    int largest_colony = 0;

    for (size_t i = 0; i < world->colony_count; i++) {
        const Colony* c = &world->colonies[i];
        if (!c->active || c->cell_count == 0) {
            continue;
        }

        const int grid_cells = count_grid_cells(world, c->id);
        if (grid_cells <= 0) {
            continue;
        }

        active_colonies++;
        total_cells += grid_cells;
        if (grid_cells > largest_colony) {
            largest_colony = grid_cells;
        }
    }

    const int world_cells = width * height;
    m.occupied_ratio = (world_cells > 0) ? (float)total_cells / (float)world_cells : 0.0f;
    m.active_colonies = (float)active_colonies;
    m.dominant_share = (total_cells > 0) ? (float)largest_colony / (float)total_cells : 0.0f;

    world_destroy(world);
    return m;
}

static void assert_threshold(const MetricThreshold* threshold, const Summary* summary) {
    const float mean_delta = fabsf(summary->mean - threshold->expected_mean);
    const float stddev_delta = fabsf(summary->stddev - threshold->expected_stddev);

    printf("STAT_REGRESSION metric=%s mean=%.6f stddev=%.6f min=%.6f max=%.6f expected_mean=%.6f mean_tol=%.6f expected_stddev=%.6f stddev_tol=%.6f\n",
           threshold->name,
           summary->mean,
           summary->stddev,
           summary->min,
           summary->max,
           threshold->expected_mean,
           threshold->mean_tolerance,
           threshold->expected_stddev,
           threshold->stddev_tolerance);

    ASSERT(mean_delta <= threshold->mean_tolerance, "mean drift exceeded tolerance");
    ASSERT(stddev_delta <= threshold->stddev_tolerance, "stddev drift exceeded tolerance");
}

static int run_statistical_regression_test(void) {
    static const uint32_t seeds[] = {
        101u, 173u, 251u, 307u, 409u, 523u, 601u, 709u,
        811u, 907u, 1009u, 1103u, 1201u, 1303u, 1409u, 1511u,
        1601u, 1709u, 1801u, 1901u, 2003u, 2111u, 2203u, 2309u,
    };

    const int seed_count = (int)(sizeof(seeds) / sizeof(seeds[0]));
    float occupied_values[24] = {0};
    float active_values[24] = {0};
    float dominant_values[24] = {0};

    for (int i = 0; i < seed_count; i++) {
        SeedMetrics m = run_seed(seeds[i]);
        occupied_values[i] = m.occupied_ratio;
        active_values[i] = m.active_colonies;
        dominant_values[i] = m.dominant_share;
    }

    const Summary occupied_summary = summarize(occupied_values, seed_count);
    const Summary active_summary = summarize(active_values, seed_count);
    const Summary dominant_summary = summarize(dominant_values, seed_count);

    static const MetricThreshold thresholds[] = {
        {
            .name = "occupied_ratio",
            .expected_mean = 0.9540f,
            .mean_tolerance = 0.0600f,
            .expected_stddev = 0.0320f,
            .stddev_tolerance = 0.0300f,
        },
        {
            .name = "active_colonies",
            .expected_mean = 22.5000f,
            .mean_tolerance = 3.0000f,
            .expected_stddev = 4.8000f,
            .stddev_tolerance = 2.2000f,
        },
        {
            .name = "dominant_share",
            .expected_mean = 0.1710f,
            .mean_tolerance = 0.0500f,
            .expected_stddev = 0.0380f,
            .stddev_tolerance = 0.0250f,
        },
    };

    assert_threshold(&thresholds[0], &occupied_summary);
    assert_threshold(&thresholds[1], &active_summary);
    assert_threshold(&thresholds[2], &dominant_summary);

    return tests_failed;
}

int main(void) {
    printf("\n=== Simulation Statistical Regression Tests ===\n");
    printf("Seeds: 24, ticks per seed: 90, world: 96x64\n");
    printf("Policy: check distribution mean + stddev for key metrics\n\n");

    int failed = run_statistical_regression_test();
    printf("\nSimulation Statistical Regression: %s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
