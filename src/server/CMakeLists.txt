add_library(ferox_server_lib STATIC
    atomic_sim.c
    genetics.c
    parallel.c
    server.c
    simulation.c
    threadpool.c
    world.c
)

target_link_libraries(ferox_server_lib PUBLIC ferox_shared Threads::Threads)
target_include_directories(ferox_server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(ferox_server main.c)
target_link_libraries(ferox_server PRIVATE ferox_server_lib)
