add_library(ferox_shared STATIC
    colors.c
    names.c
    network.c
    protocol.c
    utils.c
)

target_include_directories(ferox_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link pthread for network operations
target_link_libraries(ferox_shared PUBLIC Threads::Threads)
