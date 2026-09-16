# ===========================================================================
#  SDL2Setup.cmake — decision D1: hybrid strategy
# ===========================================================================
#
#  The subject (Chapter IV, p.6) requires:
#     "Your build output must bundle or document its runtime dependencies
#      so the corrector can run it without manual setup."
#
#  Writing "install libsdl2-dev" in the README IS manual setup, so that alone
#  would not satisfy the requirement. We therefore proceed in two stages:
#
#     1. SDL2 present on the system -> use it (fast build).
#     2. SDL2 missing               -> CMake downloads the sources and builds
#                                      SDL2 STATICALLY. The executable then
#                                      embeds SDL2 and needs no installation.
#
#  Either way this file defines a single target: RetroEmu::SDL2.
#  The rest of the project never knows which path was taken.
# ===========================================================================

option(RETROEMU_FORCE_FETCH_SDL2
       "Ignore any system SDL2 and always build SDL2 from source" OFF)

set(RETROEMU_SDL2_TAG "release-2.32.10" CACHE STRING
    "SDL2 tag used when SDL2 has to be built from source")

if(NOT TARGET RetroEmu::SDL2)

    add_library(retroemu_sdl2 INTERFACE)
    add_library(RetroEmu::SDL2 ALIAS retroemu_sdl2)

    if(NOT RETROEMU_FORCE_FETCH_SDL2)
        find_package(SDL2 QUIET)
    endif()

    if(SDL2_FOUND)
        # ---- Path 1: system SDL2 -----------------------------------------
        message(STATUS "SDL2: found on the system (version ${SDL2_VERSION})")

        if(TARGET SDL2::SDL2)
            # Modern CMake package: the target already carries its include
            # directories and compile flags.
            target_link_libraries(retroemu_sdl2 INTERFACE SDL2::SDL2)
            if(TARGET SDL2::SDL2main)
                target_link_libraries(retroemu_sdl2 INTERFACE SDL2::SDL2main)
            endif()
        else()
            # Older SDL2 packages only export variables.
            target_include_directories(retroemu_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(retroemu_sdl2 INTERFACE ${SDL2_LIBRARIES})
        endif()

        set(RETROEMU_SDL2_ORIGIN "system" CACHE INTERNAL "")

    else()
        # ---- Path 2: build from source ------------------------------------
        message(STATUS "SDL2: not found on the system -> fetching and building statically")
        message(STATUS "      (first build takes longer, afterwards it is cached)")

        include(FetchContent)

        # SDL_STATIC only: the final executable embeds SDL2 and depends on no
        # libSDL2.so at run time.
        set(SDL_SHARED             OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC             ON  CACHE BOOL "" FORCE)
        set(SDL_TEST               OFF CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_INSTALL   ON  CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_UNINSTALL ON  CACHE BOOL "" FORCE)

        FetchContent_Declare(SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG        ${RETROEMU_SDL2_TAG}
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE
        )
        FetchContent_MakeAvailable(SDL2)

        target_link_libraries(retroemu_sdl2 INTERFACE SDL2::SDL2-static)
        if(TARGET SDL2::SDL2main)
            target_link_libraries(retroemu_sdl2 INTERFACE SDL2::SDL2main)
        endif()

        set(RETROEMU_SDL2_ORIGIN "built from source (static)" CACHE INTERNAL "")
    endif()

endif()
