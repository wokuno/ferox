# GUI Client (optional - requires SDL2)
find_package(SDL2 QUIET)

if(SDL2_FOUND)
    message(STATUS "SDL2 found - building GUI client")
    
    add_executable(ferox_gui
        gui_main.c
        gui_client.c
        gui_renderer.c
        gui_input.c
    )
    
    target_include_directories(ferox_gui PRIVATE
        ${SDL2_INCLUDE_DIRS}
    )
    
    target_link_libraries(ferox_gui PRIVATE
        ferox_shared
        ${SDL2_LIBRARIES}
        m
    )
    
    # Install GUI client
    install(TARGETS ferox_gui RUNTIME DESTINATION bin)
    
else()
    # Try pkg-config as fallback
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
    
    if(SDL2_FOUND)
        message(STATUS "SDL2 found via pkg-config - building GUI client")
        
        add_executable(ferox_gui
            gui_main.c
            gui_client.c
            gui_renderer.c
            gui_input.c
        )
        
        target_include_directories(ferox_gui PRIVATE
            ${SDL2_INCLUDE_DIRS}
        )
        
        target_link_libraries(ferox_gui PRIVATE
            ferox_shared
            ${SDL2_LINK_LIBRARIES}
            m
        )
        
        target_compile_options(ferox_gui PRIVATE ${SDL2_CFLAGS_OTHER})
        
        # Install GUI client
        install(TARGETS ferox_gui RUNTIME DESTINATION bin)
    else()
        message(STATUS "SDL2 not found - GUI client will not be built")
        message(STATUS "To build the GUI client, install SDL2:")
        message(STATUS "  macOS:   brew install sdl2")
        message(STATUS "  Ubuntu:  sudo apt install libsdl2-dev")
        message(STATUS "  Windows: Download from libsdl.org")
    endif()
endif()
