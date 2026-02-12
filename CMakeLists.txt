cmake_minimum_required(VERSION 3.16)
project(ferox VERSION 1.0.0 LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_DOCS "Build documentation" OFF)
option(ENABLE_SANITIZERS "Enable address/undefined sanitizers" OFF)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Debug/Release flags
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Sanitizers (optional)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Add subdirectories
add_subdirectory(src/shared)
add_subdirectory(src/server)
add_subdirectory(src/client)
add_subdirectory(src/gui)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install targets
install(TARGETS ferox_server ferox_client
    RUNTIME DESTINATION bin
)

# CPack configuration for packaging
set(CPACK_PACKAGE_NAME "ferox")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Bacterial Colony Simulator")
include(CPack)
