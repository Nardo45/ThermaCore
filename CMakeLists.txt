cmake_minimum_required(VERSION 3.18)
project(ThermaCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# ThermaCore - root CMakeLists.text
# -----------------------------------------------------------------------------
# This file is the top-level build entry point. It does two things:
#   1. Builds the shared "thermacore_global" static library from Global/
#   2. Pulls in the TUI subdirectory which produces the thermacore-tui binary
#
# Future components (GUI, Daemon, Debugger) will be added as additional add_subdirectory()
# calls here. They will link against thermacore_global.
# =============================================================================

# ── Global static library ────────────────────────────────────────────────────
# Everything in Global/ is compiled into a single static library so that the
# TUI, GUI, Daemon, and Debugger can all link against it without duplicating
# source files.

add_library(thermacore_global STATIC
    Global/src/EmbeddedController.cpp
    Global/src/HwmonSensor.cpp
    Global/src/MachineProfile.cpp
)

# Make the Global headers available to anything that links this library
target_include_directories(thermacore_global PUBLIC
    ${CMAKE_SOURCE_DIR}/Global/include
)

target_compile_options(thermacore_global PRIVATE -Wall -Wextra -Wpedantic)

# ── Subprojects ──────────────────────────────────────────────────────────────
add_subdirectory(TUI)

# Future additions (uncomment when ready):
# add_subdirectory(GUI)
# add_subdirectory(Daemon)
# add_subdirectory(Debugger)
