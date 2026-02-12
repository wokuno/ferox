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

# Combined test runner
add_executable(test_runner test_runner.c
    test_genetics_advanced.c
    test_world_advanced.c
    test_simulation_stress.c
    test_simulation_logic.c
    test_visual_stability.c
    test_threadpool_stress.c
    test_protocol_edge.c
    test_names_exhaustive.c
    test_colors_exhaustive.c)
target_link_libraries(test_runner PRIVATE ferox_server_lib ferox_client_lib)
add_test(NAME AllTests COMMAND test_runner)
set_tests_properties(AllTests PROPERTIES TIMEOUT 300)
