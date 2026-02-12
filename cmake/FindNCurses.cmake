# FindNCurses.cmake
# Find the ncurses library
#
# This module defines:
#   NCurses_FOUND - True if ncurses was found
#   NCurses_INCLUDE_DIRS - Include directories for ncurses
#   NCurses_LIBRARIES - Libraries to link against
#   NCurses::NCurses - Imported target

find_path(NCurses_INCLUDE_DIR
    NAMES ncurses.h curses.h
    PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
    PATH_SUFFIXES ncurses
)

find_library(NCurses_LIBRARY
    NAMES ncurses curses
    PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
)

find_library(NCurses_PANEL_LIBRARY
    NAMES panel
    PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NCurses
    REQUIRED_VARS NCurses_LIBRARY NCurses_INCLUDE_DIR
)

if(NCurses_FOUND)
    set(NCurses_LIBRARIES ${NCurses_LIBRARY})
    set(NCurses_INCLUDE_DIRS ${NCurses_INCLUDE_DIR})

    if(NCurses_PANEL_LIBRARY)
        list(APPEND NCurses_LIBRARIES ${NCurses_PANEL_LIBRARY})
    endif()

    if(NOT TARGET NCurses::NCurses)
        add_library(NCurses::NCurses UNKNOWN IMPORTED)
        set_target_properties(NCurses::NCurses PROPERTIES
            IMPORTED_LOCATION "${NCurses_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${NCurses_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(NCurses_INCLUDE_DIR NCurses_LIBRARY NCurses_PANEL_LIBRARY)
