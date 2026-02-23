# Ferox Testing Documentation

This document describes the test organization, how to run tests, and guidelines for adding new tests.

## Test Summary

**15 test suites** with **200+ tests** covering:
- Core data structures and utilities
- World simulation engine (including cascade prevention)
- Genetic algorithms and recombination rules
- Threading and concurrency
- Network protocol
- Client/server implementation
- Visual stability (shape, radius, centroid)
- Stress/performance testing
- Statistical simulation regression testing (multi-seed distributions)

## Test Organization

Tests are located in the `tests/` directory:

```
tests/
├── test_phase1.c              # Core data structures & utilities (23 tests)
├── test_phase2.c              # World simulation engine (19 tests)
├── test_phase3.c              # Threading & concurrency (20 tests)
├── test_phase4.c              # Network protocol (11 tests)
├── test_phase5.c              # Server implementation
├── test_phase6.c              # Client implementation (11 tests)
├── test_genetics_advanced.c   # Extended genetics tests
├── test_world_advanced.c      # Extended world tests
├── test_simulation_logic.c    # Simulation correctness (38 tests)
├── test_simulation_stat_regression.c # Multi-seed statistical regression checks
├── test_visual_stability.c    # Visual smoothness tests (4 tests)
├── test_simulation_stress.c   # Stress/performance tests
├── test_threadpool_stress.c   # Thread pool stress tests
├── test_protocol_edge.c       # Protocol edge cases
├── test_colors_exhaustive.c   # Color utilities coverage
├── test_names_exhaustive.c    # Name generation coverage
└── test_runner.c              # Test runner utility
```

## Running Tests

### Coverage (local + CI parity)

Use CMake coverage instrumentation and `gcovr` to measure line/branch coverage:

```bash
if ! command -v uv >/dev/null 2>&1; then
  curl -LsSf https://astral.sh/uv/install.sh | sh
  export PATH="$HOME/.local/bin:$PATH"
fi

cmake -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build-coverage --parallel
cd build-coverage
ctest --output-on-failure --timeout 600 -C Debug -E "(PerformanceEvalTests|AllTests)"
cd ..
uvx gcovr --root . --print-summary --txt
uvx gcovr --root . --xml-pretty --output coverage.xml
```

Notes:
- Coverage mode is enabled via `-DENABLE_COVERAGE=ON`.
- CI runs macOS coverage on pull requests (`coverage-macos`).
- CI runs self-hosted Linux coverage only on non-PR events (`coverage-ferox`).
- CI excludes `PerformanceEvalTests` and `AllTests` during coverage runs.
- CI writes `coverage-summary.txt` and `coverage.xml`, then appends a short summary to the job summary.

### Compile and Run Individual Tests

```bash
# Phase 1: Core data structures
gcc -o tests/test_phase1 tests/test_phase1.c \
    src/shared/utils.c src/shared/names.c src/shared/colors.c
./tests/test_phase1

# Phase 2: World simulation
gcc -o tests/test_phase2 tests/test_phase2.c \
    src/server/world.c src/server/genetics.c src/server/simulation.c \
    src/shared/*.c
./tests/test_phase2

# Phase 3: Threading
gcc -pthread -o tests/test_phase3 tests/test_phase3.c \
    src/server/threadpool.c src/server/parallel.c \
    src/server/world.c src/shared/*.c
./tests/test_phase3

# Phase 4: Protocol
gcc -o tests/test_phase4 tests/test_phase4.c \
    src/shared/protocol.c src/shared/network.c
./tests/test_phase4

# Stress tests
gcc -O2 -pthread -o tests/test_simulation_stress tests/test_simulation_stress.c \
    src/server/*.c src/shared/*.c
./tests/test_simulation_stress
```

### Run All Tests

```bash
#!/bin/bash
# run_all_tests.sh

echo "=== Running All Ferox Tests ==="

for phase in 1 2 3 4 5 6; do
    if [ -f tests/test_phase${phase} ]; then
        echo ""
        echo "--- Phase $phase ---"
        ./tests/test_phase${phase}
    fi
done

echo ""
echo "=== Test Suite Complete ==="
```

### Science Benchmark Config Validation

Canonical science scenario definitions are validated by script and CTest:

```bash
python3 scripts/science_benchmarks.py validate --strict
./scripts/test.sh science
```

The check fails on malformed JSON, missing canonical scenarios, duplicate IDs, or invalid metric pass bands.

## Test Coverage Areas

### Phase 1: Core Data Structures (23 tests)

| Category | Tests |
|----------|-------|
| Random utilities | `rng_seed_deterministic`, `rand_float_range`, `rand_int_range`, `rand_range_bounds` |
| Name generation | `name_generation_format`, `name_generation_uniqueness`, `name_generation_buffer` |
| Color utilities | `hsv_to_rgb_*`, `body_color_valid_range`, `border_color_contrasting`, `color_distance_*` |
| Data structures | `genome_struct_size`, `colony_struct_init`, `cell_struct_init`, `direction_enum_values` |

### Phase 2: World Simulation (19 tests)

| Category | Tests |
|----------|-------|
| World management | `world_create_valid`, `world_create_invalid`, `world_destroy_null` |
| Cell operations | `world_get_cell_valid`, `world_get_cell_out_of_bounds` |
| Colony operations | `world_add_colony`, `world_remove_colony`, `world_get_colony` |
| Genetics | `genome_create_random`, `genome_mutate_rate`, `genome_distance`, `genome_merge` |
| Simulation | `simulation_tick`, `simulation_spread`, `simulation_divide`, `simulation_recombine` |

### Phase 3: Threading (20 tests)

| Category | Tests |
|----------|-------|
| Thread pool | `threadpool_create`, `threadpool_submit`, `threadpool_wait`, `threadpool_destroy` |
| Parallel context | `parallel_create`, `parallel_init_regions`, `parallel_spread`, `parallel_barrier` |
| Concurrency | `threadpool_multiple_tasks`, `threadpool_concurrent_submit`, `parallel_regions_correct` |

### Phase 4: Network Protocol (11 tests)

| Category | Tests |
|----------|-------|
| Header serialization | `serialize_header`, `deserialize_header`, `header_invalid_magic` |
| World serialization | `serialize_world_state`, `deserialize_world_state`, `world_roundtrip` |
| Colony serialization | `serialize_colony`, `deserialize_colony` |
| Commands | `serialize_command_*`, `deserialize_command_*` |

### Phase 5: Server Implementation

| Category | Tests |
|----------|-------|
| Server lifecycle | `server_create`, `server_destroy`, `server_run_stop` |
| Client management | `server_add_client`, `server_remove_client`, `server_process_clients` |
| Broadcasting | `server_broadcast_world_state`, `server_send_colony_info` |
| Commands | `server_handle_command_pause`, `server_handle_command_select` |

### Phase 6: Client Implementation

| Category | Tests |
|----------|-------|
| Client lifecycle | `client_create`, `client_destroy`, `client_connect`, `client_disconnect` |
| Message handling | `client_handle_world_state`, `client_handle_colony_info` |
| Rendering | `renderer_create`, `renderer_draw_world`, `renderer_present` |
| Input | `input_init`, `input_cleanup`, `input_poll` |

### Simulation Logic Tests (38 tests)

| Category | Tests |
|----------|-------|
| Cascade Prevention | `cascade_prevention_age_zero_no_spread`, `cascade_growth_rate_reasonable` |
| Recombination | `recombination_requires_relationship`, `recombination_parent_child`, `recombination_siblings`, `recombination_unrelated_no_merge` |
| Colony Growth | `smooth_colony_growth`, `no_explosive_expansion` |
| Social Behavior | `social_attraction_spreads_toward`, `social_repulsion_spreads_away`, `detection_range_limits` |
| Colony Stats | `cell_count_accuracy`, `population_tracking`, `max_population_history` |
| Colony Lifecycle | `colony_death_on_zero_population`, `colony_placement_empty_cell` |

### Visual Stability Tests (4 tests)

| Category | Tests |
|----------|-------|
| Shape stability | `shape_function_deterministic`, `shape_function_smooth_with_phase` |
| Visual metrics | `visual_radius_stability`, `centroid_stability` |

## Test Framework

Tests use a simple custom framework defined at the top of each test file:

```c
// Simple test framework
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name(void)

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Assertion failed: %s\n    At %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_LT(a, b) ASSERT((a) < (b))
#define ASSERT_LE(a, b) ASSERT((a) <= (b))
#define ASSERT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_GE(a, b) ASSERT((a) >= (b))
```

## Writing New Tests

### Test File Template

```c
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

// Include headers for code under test
#include "../src/shared/types.h"
#include "../src/server/world.h"

// Test framework (copy from existing test file)
static int tests_passed = 0;
static int tests_failed = 0;
// ... macros ...

// ============ Your Tests ============

TEST(my_new_feature_basic) {
    // Setup
    World* world = world_create(10, 10);
    ASSERT(world != NULL);
    
    // Exercise
    // ... call functions ...
    
    // Verify
    ASSERT_EQ(expected, actual);
    
    // Cleanup
    world_destroy(world);
}

TEST(my_new_feature_edge_case) {
    // Test edge cases
    ASSERT(some_function(NULL) == -1);  // NULL handling
    ASSERT(some_function_zero(0) == 0); // Zero handling
}

// ============ Main ============

int main(void) {
    printf("\n=== My New Feature Tests ===\n\n");
    
    printf("Basic Tests:\n");
    RUN_TEST(my_new_feature_basic);
    RUN_TEST(my_new_feature_edge_case);
    
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
```

### Test Naming Conventions

| Convention | Example |
|------------|---------|
| Function name | `test_<feature>_<scenario>` |
| Valid input | `test_world_create_valid` |
| Invalid input | `test_world_create_invalid_dimensions` |
| Boundary | `test_rand_range_equal_bounds` |
| Null handling | `test_world_destroy_null_safe` |

### Test Categories

1. **Unit Tests**: Test individual functions in isolation
2. **Integration Tests**: Test module interactions
3. **Stress Tests**: Test performance and stability under load

## Stress Testing

### Simulation Stress Test

Tests simulation with many colonies over many ticks:

```c
TEST(stress_simulation_many_colonies) {
    World* world = world_create(200, 200);
    world_init_random_colonies(world, 100);  // 100 colonies
    
    for (int i = 0; i < 10000; i++) {
        simulation_tick(world);
        
        // Verify invariants
        ASSERT(count_active_colonies(world) > 0);
        ASSERT(world->tick == (uint64_t)(i + 1));
    }
    
    world_destroy(world);
}
```

### Thread Pool Stress Test

Tests thread pool under concurrent load:

```c
TEST(stress_threadpool_many_tasks) {
    ThreadPool* pool = threadpool_create(8);
    atomic_int counter = 0;
    
    // Submit 10000 tasks
    for (int i = 0; i < 10000; i++) {
        threadpool_submit(pool, increment_task, &counter);
    }
    
    threadpool_wait(pool);
    ASSERT_EQ(counter, 10000);
    
    threadpool_destroy(pool);
}
```

### Memory Stress Test

Tests for memory leaks under repeated operations:

```c
TEST(stress_memory_create_destroy) {
    for (int i = 0; i < 1000; i++) {
        World* world = world_create(50, 50);
        world_init_random_colonies(world, 10);
        
        for (int j = 0; j < 100; j++) {
            simulation_tick(world);
        }
        
        world_destroy(world);
    }
    // If this completes without OOM, memory management is likely correct
}
```

## Test Output Format

```
=== Phase 1 Unit Tests ===

Random Utility Tests:
  Running rng_seed_deterministic... PASSED
  Running rand_float_range... PASSED
  Running rand_int_range... PASSED
  Running rand_int_zero_max... PASSED
  Running rand_range_bounds... PASSED
  Running rand_range_equal_bounds... PASSED

Name Generation Tests:
  Running name_generation_format... PASSED
  Running name_generation_uniqueness... PASSED
  Running name_generation_buffer... PASSED

Color Tests:
  Running hsv_to_rgb_red... PASSED
  Running hsv_to_rgb_green... PASSED
  ...

=== Results ===
Passed: 23
Failed: 0
```

### Failure Output

```
  Running world_get_cell_bounds... FAILED
    Assertion failed: cell != NULL
    At tests/test_phase2.c:142
```

## Continuous Integration

### Current CI Workflow

CI is defined in `.github/workflows/ci.yml`:

- `build-and-test-macos`: runs on `macos-latest` for `Release` and `Debug`.
- `build-and-test-ferox`: runs on self-hosted labels `self-hosted`, `Linux`, `X64`, `ferox`, `rocky10` for `Release` and `Debug`.
- Build jobs run `SimulationStatRegressionTests` and publish metric summaries in the GitHub Actions job summary.
- `coverage-macos`: runs on `macos-latest` with `-DENABLE_COVERAGE=ON`, excludes `PerformanceEvalTests|AllTests`, and generates `coverage-summary.txt` + `coverage.xml` with `uvx gcovr`.
- `coverage-ferox`: runs on self-hosted Linux for non-PR events with the same coverage configuration.
- `pr-test-summary`: runs only on pull requests and posts a consolidated comment with build and coverage job status.

## Debugging Failed Tests

### Using GDB

```bash
gcc -g -o tests/test_debug tests/test_phase2.c src/server/*.c src/shared/*.c
gdb ./tests/test_debug

(gdb) run
(gdb) bt  # backtrace on failure
```

### Using Valgrind

```bash
gcc -g -o tests/test_memcheck tests/test_phase2.c src/server/*.c src/shared/*.c
valgrind --leak-check=full ./tests/test_memcheck
```

### Using AddressSanitizer

```bash
gcc -fsanitize=address -g -o tests/test_asan tests/test_phase2.c \
    src/server/*.c src/shared/*.c
./tests/test_asan
```

## Test Checklist for New Features

- [ ] Unit tests for all public functions
- [ ] Tests for NULL/invalid input handling
- [ ] Tests for boundary conditions
- [ ] Tests for error conditions
- [ ] Integration tests with related modules
- [ ] Stress tests for performance-critical code
- [ ] Memory leak tests (valgrind)
- [ ] Thread safety tests (for concurrent code)
