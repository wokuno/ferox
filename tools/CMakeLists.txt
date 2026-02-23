add_executable(ferox_preview_capture
    preview_capture.c
)

target_link_libraries(ferox_preview_capture PRIVATE ferox_server_lib)
