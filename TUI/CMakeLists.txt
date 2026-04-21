cmake_minimum_required(VERSION 3.18)

# =============================================================================
# TUI/CMakeLists.txt
# -----------------------------------------------------------------------------
# Builds the thermacore-tui executable.
#
# Source files:
#   src/FanController.cpp  — fan state polling and command processing
#   src/main.cpp           — ncurses loop, entry point
#
# Dependencies:
#   thermacore_global      — EC driver, hwmon, machine profiles (from root)
#   ncurses                — terminal UI library
#   stdc++fs               — C++17 std::filesystem (may be implicit on GCC 9+)
# =============================================================================

# Find ncurses on the system
find_package(Curses REQUIRED)

add_executable(thermacore-tui
    src/FanController.cpp
    src/main.cpp
)

target_include_directories(thermacore-tui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include  # TUI/include/  (FanController.hpp)
)

target_link_libraries(thermacore-tui PRIVATE
    thermacore_global
    ncursesw
    stdc++fs
)

target_compile_options(thermacore-tui PRIVATE -Wall -Wextra -Wpedantic)
