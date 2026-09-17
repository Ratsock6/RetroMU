# ===========================================================================
#  RetroEmu — 8-bit handheld console emulator (DMG and CGB)
# ===========================================================================
#  Build, in a SINGLE command as required by the subject (Chapter IV, p.6):
#
#      cmake -B build && cmake --build build -j
#
#  Run:
#
#      ./build/retroemu
# ===========================================================================

cmake_minimum_required(VERSION 3.16)

project(RetroEmu
        VERSION     0.1.0
        DESCRIPTION "8-bit handheld console emulator (DMG / CGB)"
        LANGUAGES   CXX)

# --- Language --------------------------------------------------------------
# C++17: enough for the whole project, available everywhere, and no risk that
# the corrector's compiler is too old.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Default build type ----------------------------------------------------
# Without -O2 the emulation is too slow to hold the normal speed required by
# section V.3 of the subject (p.7), so Release is the default.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

# Export compile_commands.json for clangd and editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Project options -------------------------------------------------------
option(RETROEMU_WARNINGS  "Enable strict compiler warnings"           ON)
option(RETROEMU_SANITIZE  "Enable sanitizers (address + undefined)"   OFF)

# --- Dependency imposed by the subject: SDL2 (p.6) --------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SDL2Setup)

# --- The executable --------------------------------------------------------
add_executable(retroemu
    src/main.cpp
    src/core/cartridge.cpp
    src/core/bus.cpp
    src/core/cpu.cpp
    src/debug/cpu_selftest.cpp
    src/debug/disassembler.cpp
    src/debug/debugger.cpp
    src/debug/tracer.cpp
    src/core/gameboy.cpp
    src/core/ppu.cpp
)

target_include_directories(retroemu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(retroemu PRIVATE RetroEmu::SDL2)

if(RETROEMU_WARNINGS)
    target_compile_options(retroemu PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
endif()

if(RETROEMU_SANITIZE)
    # The 'undefined' sanitizer catches overflows and invalid shifts, which is
    # invaluable in an 8/16-bit emulator.
    target_compile_options(retroemu PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(retroemu    PRIVATE -fsanitize=address,undefined)
endif()

# --- Configuration summary -------------------------------------------------
message(STATUS "")
message(STATUS "  RetroEmu ${PROJECT_VERSION}")
message(STATUS "  ---------------------------------------------")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  SDL2       : ${RETROEMU_SDL2_ORIGIN}")
message(STATUS "  Sanitizers : ${RETROEMU_SANITIZE}")
message(STATUS "  ---------------------------------------------")
message(STATUS "")
