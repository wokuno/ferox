# Test executables for each phase

# Phase 1 - Basic shared library tests
add_executable(test_phase1 test_phase1.c)
target_link_libraries(test_phase1 PRIVATE ferox_shared)
add_test(NAME Phase1Tests COMMAND test_phase1)

# Phase 2 - Server library tests
add_executable(test_phase2 test_phase2.c)
target_link_libraries(test_phase2 PRIVATE ferox_server_lib)
add_test(NAME Phase2Tests COMMAND test_phase2)

# Phase 3 - Additional server tests
add_executable(test_phase3 test_phase3.c)
target_link_libraries(test_phase3 PRIVATE ferox_server_lib)
add_test(NAME Phase3Tests COMMAND test_phase3)

# Phase 4 - Integration tests
add_executable(test_phase4 test_phase4.c)
target_link_libraries(test_phase4 PRIVATE ferox_server_lib)
add_test(NAME Phase4Tests COMMAND test_phase4)

# Phase 5 - Client tests
add_executable(test_phase5 test_phase5.c)
target_link_libraries(test_phase5 PRIVATE ferox_client_lib ferox_server_lib)
add_test(NAME Phase5Tests COMMAND test_phase5)

# Client logic surface tests
add_executable(test_client_logic_surface test_client_logic_surface.c)
add_test(NAME ClientLogicSurfaceTests COMMAND test_client_logic_surface)

# Phase 6 - Full system tests
add_executable(test_phase6 test_phase6.c)
target_link_libraries(test_phase6 PRIVATE ferox_client_lib ferox_server_lib)
add_test(NAME Phase6Tests COMMAND test_phase6)

# Advanced genetics tests
add_executable(test_genetics_advanced test_genetics_advanced.c)
target_link_libraries(test_genetics_advanced PRIVATE ferox_server_lib)
target_compile_definitions(test_genetics_advanced PRIVATE STANDALONE_TEST)
add_test(NAME GeneticsAdvancedTests COMMAND test_genetics_advanced)

# Advanced world tests
add_executable(test_world_advanced test_world_advanced.c)
target_link_libraries(test_world_advanced PRIVATE ferox_server_lib)
target_compile_definitions(test_world_advanced PRIVATE STANDALONE_TEST)
add_test(NAME WorldAdvancedTests COMMAND test_world_advanced)

# Simulation stress tests
add_executable(test_simulation_stress test_simulation_stress.c)
target_link_libraries(test_simulation_stress PRIVATE ferox_server_lib)
target_compile_definitions(test_simulation_stress PRIVATE STANDALONE_TEST)
add_test(NAME SimulationStressTests COMMAND test_simulation_stress)

# Set test properties for longer-running tests
set_tests_properties(SimulationStressTests PROPERTIES TIMEOUT 120)

# Threadpool stress tests
add_executable(test_threadpool_stress test_threadpool_stress.c)
target_link_libraries(test_threadpool_stress PRIVATE ferox_server_lib)
target_compile_definitions(test_threadpool_stress PRIVATE STANDALONE_TEST)
add_test(NAME ThreadpoolStressTests COMMAND test_threadpool_stress)
set_tests_properties(ThreadpoolStressTests PROPERTIES TIMEOUT 120)

# Protocol edge case tests
add_executable(test_protocol_edge test_protocol_edge.c)
target_link_libraries(test_protocol_edge PRIVATE ferox_server_lib)
target_compile_definitions(test_protocol_edge PRIVATE STANDALONE_TEST)
add_test(NAME ProtocolEdgeTests COMMAND test_protocol_edge)

# Exhaustive name tests
add_executable(test_names_exhaustive test_names_exhaustive.c)
target_link_libraries(test_names_exhaustive PRIVATE ferox_shared)
target_compile_definitions(test_names_exhaustive PRIVATE STANDALONE_TEST)
add_test(NAME NamesExhaustiveTests COMMAND test_names_exhaustive)

# Exhaustive color tests
add_executable(test_colors_exhaustive test_colors_exhaustive.c)
target_link_libraries(test_colors_exhaustive PRIVATE ferox_shared)
target_compile_definitions(test_colors_exhaustive PRIVATE STANDALONE_TEST)
add_test(NAME ColorsExhaustiveTests COMMAND test_colors_exhaustive)

# Simulation logic tests
add_executable(test_simulation_logic test_simulation_logic.c)
target_link_libraries(test_simulation_logic PRIVATE ferox_server_lib)
target_compile_definitions(test_simulation_logic PRIVATE STANDALONE_TEST)
add_test(NAME SimulationLogicTests COMMAND test_simulation_logic)

# Visual stability stress tests
add_executable(test_visual_stability test_visual_stability.c)
target_link_libraries(test_visual_stability PRIVATE ferox_server_lib)
target_compile_definitions(test_visual_stability PRIVATE STANDALONE_TEST)
add_test(NAME VisualStabilityTests COMMAND test_visual_stability)
set_tests_properties(VisualStabilityTests PROPERTIES TIMEOUT 300)

# SIMD correctness and performance evaluation tests
add_executable(test_simd_eval test_simd_eval.c)
target_link_libraries(test_simd_eval PRIVATE ferox_server_lib)
target_compile_definitions(test_simd_eval PRIVATE STANDALONE_TEST)
add_test(NAME SimdEvalTests COMMAND test_simd_eval)
set_tests_properties(SimdEvalTests PROPERTIES TIMEOUT 120)

# Reaction-diffusion solver controls tests
add_executable(test_rd_controls test_rd_controls.c)
target_link_libraries(test_rd_controls PRIVATE ferox_server_lib)
target_compile_definitions(test_rd_controls PRIVATE STANDALONE_TEST)
add_test(NAME RdControlsTests COMMAND test_rd_controls)

# World branch coverage tests
add_executable(test_world_branch_coverage test_world_branch_coverage.c)
target_link_libraries(test_world_branch_coverage PRIVATE ferox_server_lib)
target_compile_definitions(test_world_branch_coverage PRIVATE STANDALONE_TEST)
add_test(NAME WorldBranchCoverageTests COMMAND test_world_branch_coverage)

# Server branch coverage tests
add_executable(test_server_branch_coverage test_server_branch_coverage.c)
target_link_libraries(test_server_branch_coverage PRIVATE ferox_server_lib)
target_compile_definitions(test_server_branch_coverage PRIVATE STANDALONE_TEST)
add_test(NAME ServerBranchCoverageTests COMMAND test_server_branch_coverage)

# Broad performance evaluation tests
add_executable(test_performance_eval test_performance_eval.c)
target_link_libraries(test_performance_eval PRIVATE ferox_server_lib)
target_compile_definitions(test_performance_eval PRIVATE STANDALONE_TEST)
add_test(NAME PerformanceEvalTests COMMAND test_performance_eval)
set_tests_properties(PerformanceEvalTests PROPERTIES TIMEOUT 180)

# Focused component-level performance evaluation tests
add_executable(test_perf_components test_perf_components.c)
target_link_libraries(test_perf_components PRIVATE ferox_server_lib)
target_compile_definitions(test_perf_components PRIVATE STANDALONE_TEST)
add_test(NAME PerformanceComponentTests COMMAND test_perf_components)
set_tests_properties(PerformanceComponentTests PROPERTIES TIMEOUT 180)
# Combat system tests
add_executable(test_combat_system test_combat_system.c)
target_link_libraries(test_combat_system PRIVATE ferox_server_lib)
target_compile_definitions(test_combat_system PRIVATE STANDALONE_TEST)
add_test(NAME CombatSystemTests COMMAND test_combat_system)
set_tests_properties(CombatSystemTests PROPERTIES TIMEOUT 120)

# GUI unit tests (no SDL dependency - tests logic only)
add_executable(test_gui test_gui.c)
target_link_libraries(test_gui PRIVATE m)
target_compile_definitions(test_gui PRIVATE STANDALONE_TEST)
add_test(NAME GuiTests COMMAND test_gui)

# Hardware profile tests
add_executable(test_hardware_profile test_hardware_profile.c)
target_link_libraries(test_hardware_profile PRIVATE ferox_server_lib)
target_compile_definitions(test_hardware_profile PRIVATE STANDALONE_TEST)
add_test(NAME HardwareProfileTests COMMAND test_hardware_profile)

# Performance profiling tests (profiling-oriented, reports hotspots)
add_executable(test_performance_profile test_performance_profile.c)
target_link_libraries(test_performance_profile PRIVATE ferox_server_lib)
target_compile_definitions(test_performance_profile PRIVATE STANDALONE_TEST)
add_test(NAME PerformanceProfilingTests COMMAND test_performance_profile)
set_tests_properties(PerformanceProfilingTests PROPERTIES TIMEOUT 180
    ENVIRONMENT "FEROX_THREADPOOL_PROFILE=balanced")

# Dedicated threadpool microbench (performance diagnostics)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_threadpool_microbench.c")
    add_executable(test_threadpool_microbench test_threadpool_microbench.c)
    target_link_libraries(test_threadpool_microbench PRIVATE ferox_server_lib)
    target_compile_definitions(test_threadpool_microbench PRIVATE STANDALONE_TEST)
    add_test(NAME ThreadpoolMicrobenchTests COMMAND test_threadpool_microbench)
    set_tests_properties(ThreadpoolMicrobenchTests PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
        ENVIRONMENT "FEROX_THREADPOOL_MICROBENCH_MODE=report;FEROX_THREADPOOL_PROFILE=balanced")
endif()

# Threadpool profile scan (diagnostic benchmark matrix)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_threadpool_profile_scan.c")
    add_executable(test_threadpool_profile_scan test_threadpool_profile_scan.c)
    target_link_libraries(test_threadpool_profile_scan PRIVATE ferox_server_lib)
    target_compile_definitions(test_threadpool_profile_scan PRIVATE STANDALONE_TEST)
    add_test(NAME ThreadpoolProfileScanTests COMMAND test_threadpool_profile_scan)
    set_tests_properties(ThreadpoolProfileScanTests PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
endif()

# Unit-level performance diagnostics: world lookups/churn
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_perf_unit_world.c")
    add_executable(test_perf_unit_world test_perf_unit_world.c)
    target_link_libraries(test_perf_unit_world PRIVATE ferox_server_lib)
    target_compile_definitions(test_perf_unit_world PRIVATE STANDALONE_TEST)
    add_test(NAME PerfUnitWorldTests COMMAND test_perf_unit_world)
    set_tests_properties(PerfUnitWorldTests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endif()

# Unit-level performance diagnostics: protocol grid codec
add_executable(test_perf_unit_protocol test_perf_unit_protocol.c)
target_link_libraries(test_perf_unit_protocol PRIVATE ferox_server_lib)
target_compile_definitions(test_perf_unit_protocol PRIVATE STANDALONE_TEST)
add_test(NAME PerfUnitProtocolTests COMMAND test_perf_unit_protocol)
set_tests_properties(PerfUnitProtocolTests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# Component-level performance diagnostics: atomic tick modes
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_perf_component_atomic.c")
    add_executable(test_perf_component_atomic test_perf_component_atomic.c)
    target_link_libraries(test_perf_component_atomic PRIVATE ferox_server_lib)
    target_compile_definitions(test_perf_component_atomic PRIVATE STANDALONE_TEST)
    add_test(NAME PerfComponentAtomicTests COMMAND test_perf_component_atomic)
    set_tests_properties(PerfComponentAtomicTests PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
endif()

# Combined test runner
add_executable(test_runner test_runner.c
    test_genetics_advanced.c
    test_world_advanced.c
    test_simulation_stress.c
    test_simulation_logic.c
    test_simd_eval.c
    test_performance_eval.c
    test_perf_components.c
    test_growth_shapes.c
    test_visual_stability.c
    test_threadpool_stress.c
    test_protocol_edge.c
    test_names_exhaustive.c
    test_colors_exhaustive.c)
target_link_libraries(test_runner PRIVATE ferox_server_lib ferox_client_lib)
add_test(NAME AllTests COMMAND test_runner)
set_tests_properties(AllTests PROPERTIES TIMEOUT 300)
