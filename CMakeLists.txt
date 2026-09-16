# ===========================================================================
#  RetroEmu — emulateur de console portable 8 bits (DMG et CGB)
# ===========================================================================
#  Compilation, en UNE SEULE commande (exigence du sujet, Chapitre IV p.6) :
#
#      cmake -B build && cmake --build build -j
#
#  Lancement :
#
#      ./build/retroemu
# ===========================================================================

cmake_minimum_required(VERSION 3.16)

project(RetroEmu
        VERSION     0.1.0
        DESCRIPTION "Emulateur de console portable 8 bits (DMG / CGB)"
        LANGUAGES   CXX)

# --- Langage ---------------------------------------------------------------
# C++17 : suffisant pour tout le projet, disponible partout, et sans risque
# que la machine du correcteur ait un compilateur trop ancien.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Type de build par defaut ---------------------------------------------
# Sans -O2, l'emulation est trop lente pour tenir la vitesse normale exigee
# par la section V.3 du sujet (p.7). Release est donc le defaut.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

# Exporte compile_commands.json pour clangd / les editeurs.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Options du projet -----------------------------------------------------
option(RETROEMU_WARNINGS  "Activer les avertissements stricts"            ON)
option(RETROEMU_SANITIZE  "Activer les sanitizers (address + undefined)" OFF)

# --- Dependance imposee par le sujet : SDL2 (p.6) ---------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SDL2Setup)

# --- L'executable ----------------------------------------------------------
add_executable(retroemu
    src/main.cpp
)

target_include_directories(retroemu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(retroemu PRIVATE RetroEmu::SDL2)

if(RETROEMU_WARNINGS)
    target_compile_options(retroemu PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
endif()

if(RETROEMU_SANITIZE)
    # Le sanitizer 'undefined' attrape les debordements et les decalages
    # invalides : precieux sur un emulateur 8/16 bits.
    target_compile_options(retroemu PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(retroemu    PRIVATE -fsanitize=address,undefined)
endif()

# --- Recapitulatif affiche a la configuration ------------------------------
message(STATUS "")
message(STATUS "  RetroEmu ${PROJECT_VERSION}")
message(STATUS "  ---------------------------------------------")
message(STATUS "  Type de build : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compilateur   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  SDL2          : ${RETROEMU_SDL2_ORIGINE}")
message(STATUS "  Sanitizers    : ${RETROEMU_SANITIZE}")
message(STATUS "  ---------------------------------------------")
message(STATUS "")
