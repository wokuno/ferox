# Visual Stability Stress Tests

## Overview

Created comprehensive stress tests for the bacterial colony simulator (`test_visual_stability.c`) to ensure visual properties remain stable across different scales and update frequencies.

## Test Categories

### 1. Small Scale Tests (10x10 world, 3 colonies, 50 ticks)
- **small_scale_shape_seed_valid**: Ensures all colonies have valid (non-zero) shape seeds
- **small_scale_centroid_bounded**: Verifies colony centroids don't jump more than 3 cells per tick
- **small_scale_radius_bounded**: Checks radius changes are bounded and stay within world size
- **small_scale_wobble_phase_smooth**: Validates wobble_phase increments smoothly (max jump ~0.1 per tick)
- **small_scale_no_unexpected_colonies**: Confirms colony count stays reasonable

### 2. Medium Scale Tests (100x100 world, 20 colonies, 200 ticks)
- **medium_scale_all_properties_valid**: Verifies shape_seed, cell_count consistency, and position bounds
- **medium_scale_radius_within_bounds**: Ensures radius never exceeds world size
- **medium_scale_population_consistency**: Validates cell counts match grid and max_cell_count tracking
- **medium_scale_no_duplicate_colonies**: Checks for duplicate colony IDs

### 3. Large Scale Tests (200x100 world, 50 colonies, 500 ticks)
- **large_scale_extreme_value_protection**: Detects extreme values and out-of-bounds positions
- **large_scale_shape_seed_valid**: Ensures shape seeds remain valid throughout long simulation
- **large_scale_cell_allocation_bounds**: Verifies total cells used never exceeds world capacity

### 4. Rapid Update Tests (30 FPS client vs 10 Hz server simulation)
- **rapid_update_shape_seed_valid**: Validates shape_seed remains non-zero during rapid updates
- **rapid_update_radius_reasonable**: Allows significant radius changes during growth/division

### 5. Division/Recombination Stress Tests
- **division_properties_valid**: Ensures all colonies have valid properties after divisions
- **recombination_properties_stable**: Validates properties during colony merging
- **child_colony_initialization**: Checks child colonies have proper initialization

## Test Framework

The tests use a simple assertion-based framework compatible with the existing test suite:

```c
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) // Runs test and tracks pass/fail
#define ASSERT_EQ(a, b) // Equality checks
#define ASSERT_NE(a, b) // Not equal checks
#define ASSERT_LE(a, b) // Less than or equal checks
// ... and more
```

## Helper Functions

- `create_test_colony()`: Creates a test colony with random genome
- `count_colony_cells()`: Counts cells belonging to a colony
- `calc_centroid()`: Calculates colony center of mass
- `calc_radius()`: Calculates farthest distance from centroid
- `take_snapshot()`: Records all colony properties at a point in time

## Building

Added to `tests/CMakeLists.txt`:
```cmake
add_executable(test_visual_stability test_visual_stability.c)
target_link_libraries(test_visual_stability PRIVATE ferox_server_lib)
target_compile_definitions(test_visual_stability PRIVATE STANDALONE_TEST)
add_test(NAME VisualStabilityTests COMMAND test_visual_stability)
set_tests_properties(VisualStabilityTests PROPERTIES TIMEOUT 300)
```

Also added to combined test runner.

## Running Tests

```bash
# Run visual stability tests only
cd build && ./tests/test_visual_stability

# Run via ctest
cd build && ctest -R VisualStability -V

# Run all tests including visual stability
cd build && ctest --output-on-failure
```

## Results

✅ All 17 tests pass
- 5 small-scale tests
- 4 medium-scale tests  
- 3 large-scale tests
- 2 rapid update tests
- 3 division/recombination tests

## Key Validation Points

### Shape Stability
- Shape seeds never become zero
- Shape seeds can change (mutations/divisions are expected)
- Changes are controlled and deterministic

### Visual Coherence
- Centroids move smoothly (max 3 cell jump per tick)
- Radius changes are bounded (allow up to 10x during growth)
- Positions stay within world bounds

### Population Tracking
- Cell counts match actual grid contents
- No duplicate colony IDs
- max_cell_count never decreases
- Total cells never exceed world capacity

### Wobble Animation
- Phase increments smoothly (max ~0.1 per tick)
- Stays in valid range [0, 2π]
- Wraps correctly at boundaries

## Future Improvements

1. Add performance metrics tracking
2. Monitor memory stability across long runs
3. Track specific mutation patterns
4. Validate visual rendering output
5. Stress test with extreme parameters
