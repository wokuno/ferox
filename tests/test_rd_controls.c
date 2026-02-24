#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../src/server/world.h"
#include "../src/server/simulation.h"

extern void simulation_decay_toxins(World* world);

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED\\n    %s\\n    At %s:%d\\n", msg, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) <= (eps), #a " ~= " #b)

TEST(custom_toxin_decay_uses_configured_coefficient) {
    World* world = world_create(4, 1);
    ASSERT(world != NULL, "world create");

    RDSolverControls controls = world_get_rd_controls(world);
    controls.toxins.diffusion = 0.0f;
    controls.toxins.decay = 0.25f;
    ASSERT(world_set_rd_controls(world, &controls, NULL, 0), "set controls");

    world->toxins[0] = 1.0f;
    world->toxins[1] = 0.6f;
    world->toxins[2] = 0.2f;
    world->toxins[3] = 0.0f;

    simulation_decay_toxins(world);

    ASSERT_NEAR(world->toxins[0], 0.75f, 1e-6f);
    ASSERT_NEAR(world->toxins[1], 0.45f, 1e-6f);
    ASSERT_NEAR(world->toxins[2], 0.15f, 1e-6f);
    ASSERT_NEAR(world->toxins[3], 0.0f, 1e-6f);

    world_destroy(world);
}

TEST(signal_diffusion_decay_matches_reference_stencil) {
    World* world = world_create(3, 3);
    ASSERT(world != NULL, "world create");

    RDSolverControls controls = world_get_rd_controls(world);
    controls.signals.diffusion = 0.1f;
    controls.signals.decay = 0.2f;
    ASSERT(world_set_rd_controls(world, &controls, NULL, 0), "set controls");

    memset(world->signals, 0, (size_t)(world->width * world->height) * sizeof(float));
    world->signals[4] = 1.0f;  // center pulse

    simulation_update_scents(world);

    ASSERT_NEAR(world->signals[4], 0.4f, 1e-6f);
    ASSERT_NEAR(world->signals[1], 0.1f, 1e-6f);
    ASSERT_NEAR(world->signals[3], 0.1f, 1e-6f);
    ASSERT_NEAR(world->signals[5], 0.1f, 1e-6f);
    ASSERT_NEAR(world->signals[7], 0.1f, 1e-6f);

    world_destroy(world);
}

TEST(unstable_solver_controls_are_rejected) {
    World* world = world_create(8, 8);
    ASSERT(world != NULL, "world create");

    RDSolverControls controls = world_get_rd_controls(world);
    controls.signals.diffusion = 0.24f;
    controls.signals.decay = 0.1f;

    char err[256];
    bool ok = world_set_rd_controls(world, &controls, err, sizeof(err));
    ASSERT(!ok, "invalid controls should fail");
    ASSERT(strstr(err, "unstable") != NULL, "expected unstable diagnostic");

    world_destroy(world);
}

int run_rd_controls_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("\n=== Reaction-Diffusion Controls Tests ===\n\n");
    RUN_TEST(custom_toxin_decay_uses_configured_coefficient);
    RUN_TEST(signal_diffusion_decay_matches_reference_stencil);
    RUN_TEST(unstable_solver_controls_are_rejected);

    printf("\n--- Reaction-Diffusion Controls Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_rd_controls_tests() > 0 ? 1 : 0;
}
#endif
