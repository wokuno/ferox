add_library(ferox_client_lib STATIC
    client.c
    input.c
    renderer.c
)

target_link_libraries(ferox_client_lib PUBLIC ferox_shared)
target_include_directories(ferox_client_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(ferox_client main.c)
target_link_libraries(ferox_client PRIVATE ferox_client_lib)
