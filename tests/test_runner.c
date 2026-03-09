/**
 * test_runner.c - Main test runner for all bacterial colony simulator tests
 * Runs all test suites and reports aggregate results
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/shared/utils.h"

// Declare test runner functions from each test file
extern int run_genetics_advanced_tests(void);
extern int run_world_advanced_tests(void);
extern int run_simulation_stress_tests(void);
extern int run_simulation_logic_tests(void);
extern int run_simd_eval_tests(void);
extern int run_performance_eval_tests(void);
extern int run_perf_component_tests(void);
extern int run_threadpool_stress_tests(void);
extern int run_protocol_edge_tests(void);
extern int run_names_exhaustive_tests(void);
extern int run_colors_exhaustive_tests(void);
extern int run_growth_shapes_tests(void);

// Track overall results
typedef struct {
    const char* name;
    int failures;
    int run;
} TestSuiteResult;

int main(int argc, char* argv[]) {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║        Ferox Bacterial Colony Simulator - Test Suite         ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Running comprehensive tests across all modules...           ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    // Initialize random seed for reproducibility
    rng_seed(42);
    
    TestSuiteResult results[13];
    int suite_index = 0;
    int total_failures = 0;
    
    // Check if specific test suite requested
    const char* specific_suite = NULL;
    if (argc > 1) {
        specific_suite = argv[1];
        printf("\nRunning specific suite: %s\n", specific_suite);
    }
    
    // Run each test suite
    
    // 1. Genetics Advanced Tests
    if (!specific_suite || strcmp(specific_suite, "genetics") == 0) {
        results[suite_index].name = "Genetics Advanced";
        results[suite_index].failures = run_genetics_advanced_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 2. World Advanced Tests
    if (!specific_suite || strcmp(specific_suite, "world") == 0) {
        results[suite_index].name = "World Advanced";
        results[suite_index].failures = run_world_advanced_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 3. Simulation Stress Tests
    if (!specific_suite || strcmp(specific_suite, "simulation") == 0) {
        results[suite_index].name = "Simulation Stress";
        results[suite_index].failures = run_simulation_stress_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 4. Simulation Logic Tests
    if (!specific_suite || strcmp(specific_suite, "logic") == 0) {
        results[suite_index].name = "Simulation Logic";
        results[suite_index].failures = run_simulation_logic_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 5. SIMD Eval Tests
    if (!specific_suite || strcmp(specific_suite, "simd") == 0) {
        results[suite_index].name = "SIMD Eval";
        results[suite_index].failures = run_simd_eval_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 6. Performance Eval Tests
    if (!specific_suite || strcmp(specific_suite, "perf") == 0) {
        results[suite_index].name = "Performance Eval";
        results[suite_index].failures = run_performance_eval_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 7. Thread Pool Stress Tests
    if (!specific_suite || strcmp(specific_suite, "perf-components") == 0) {
        results[suite_index].name = "Performance Components";
        results[suite_index].failures = run_perf_component_tests();
        results[suite_index].run = 1;
        suite_index++;
    }

    // 8. Thread Pool Stress Tests
    if (!specific_suite || strcmp(specific_suite, "threadpool") == 0) {
        results[suite_index].name = "Thread Pool Stress";
        results[suite_index].failures = run_threadpool_stress_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 9. Protocol Edge Tests
    if (!specific_suite || strcmp(specific_suite, "protocol") == 0) {
        results[suite_index].name = "Protocol Edge";
        results[suite_index].failures = run_protocol_edge_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 10. Names Exhaustive Tests
    if (!specific_suite || strcmp(specific_suite, "names") == 0) {
        results[suite_index].name = "Names Exhaustive";
        results[suite_index].failures = run_names_exhaustive_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 11. Colors Exhaustive Tests
    if (!specific_suite || strcmp(specific_suite, "colors") == 0) {
        results[suite_index].name = "Colors Exhaustive";
        results[suite_index].failures = run_colors_exhaustive_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // 12. Growth Shape Tests
    if (!specific_suite || strcmp(specific_suite, "growth") == 0) {
        results[suite_index].name = "Growth Shapes";
        results[suite_index].failures = run_growth_shapes_tests();
        results[suite_index].run = 1;
        suite_index++;
    }
    
    // Print summary
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                       TEST SUMMARY                           ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    for (int i = 0; i < suite_index; i++) {
        const char* status = results[i].failures == 0 ? "✓ PASS" : "✗ FAIL";
        printf("║  %-25s %s (%d failures)         ║\n",
               results[i].name, status, results[i].failures);
        total_failures += results[i].failures;
    }
    
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    if (total_failures == 0) {
        printf("║                   🎉 ALL TESTS PASSED! 🎉                    ║\n");
    } else {
        printf("║           ⚠️  %3d TEST(S) FAILED - SEE ABOVE  ⚠️              ║\n", total_failures);
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    return total_failures > 0 ? 1 : 0;
}
