add_library(ferox_shared STATIC
    colors.c
    names.c
    network.c
    protocol.c
    simulation_common.c
    utils.c
)

target_include_directories(ferox_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ferox_shared PRIVATE ${CMAKE_SOURCE_DIR}/src/server)

# Link pthread for network operations and math library
target_link_libraries(ferox_shared PUBLIC Threads::Threads m)
