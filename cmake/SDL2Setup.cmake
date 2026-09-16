# ===========================================================================
#  SDL2Setup.cmake — decision D1 du plan : strategie hybride
# ===========================================================================
#
#  Le sujet (Chapitre IV, p.6) exige :
#     « Your build output must bundle or document its runtime dependencies
#       so the corrector can run it without manual setup. »
#
#  Dire « installez libsdl2-dev » dans le README, c'est du setup manuel :
#  l'exigence ne serait pas tenue. On procede donc en deux temps :
#
#     1. SDL2 present sur le systeme  -> on l'utilise (build rapide).
#     2. SDL2 absent                  -> CMake telecharge les sources et
#                                        compile SDL2 en STATIQUE. L'executable
#                                        embarque alors SDL2 : aucune
#                                        installation n'est requise.
#
#  Dans les deux cas, ce fichier definit une cible unique : RetroEmu::SDL2.
#  Le reste du projet ne sait pas laquelle des deux voies a ete empruntee.
# ===========================================================================

option(RETROEMU_FORCE_FETCH_SDL2
       "Ignorer le SDL2 du systeme et toujours compiler SDL2 depuis les sources" OFF)

set(RETROEMU_SDL2_TAG "release-2.32.10" CACHE STRING
    "Tag SDL2 utilise quand SDL2 doit etre compile depuis les sources")

if(NOT TARGET RetroEmu::SDL2)

    add_library(retroemu_sdl2 INTERFACE)
    add_library(RetroEmu::SDL2 ALIAS retroemu_sdl2)

    if(NOT RETROEMU_FORCE_FETCH_SDL2)
        find_package(SDL2 QUIET)
    endif()

    if(SDL2_FOUND)
        # ---- Voie 1 : SDL2 du systeme ------------------------------------
        message(STATUS "SDL2 : trouve sur le systeme (version ${SDL2_VERSION})")

        if(TARGET SDL2::SDL2)
            # CMake moderne : la cible porte deja ses includes et ses flags.
            target_link_libraries(retroemu_sdl2 INTERFACE SDL2::SDL2)
            if(TARGET SDL2::SDL2main)
                target_link_libraries(retroemu_sdl2 INTERFACE SDL2::SDL2main)
            endif()
        else()
            # Anciens paquets SDL2 : seules les variables sont fournies.
            target_include_directories(retroemu_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(retroemu_sdl2 INTERFACE ${SDL2_LIBRARIES})
        endif()

        set(RETROEMU_SDL2_ORIGINE "systeme" CACHE INTERNAL "")

    else()
        # ---- Voie 2 : compilation depuis les sources ----------------------
        message(STATUS "SDL2 : absent du systeme -> telechargement et compilation statique")
        message(STATUS "       (premiere compilation plus longue, ensuite mis en cache)")

        include(FetchContent)

        # SDL_STATIC seul : l'executable final embarque SDL2 et ne depend
        # d'aucun libSDL2.so a l'execution.
        set(SDL_SHARED           OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC           ON  CACHE BOOL "" FORCE)
        set(SDL_TEST             OFF CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_INSTALL ON  CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)

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

        set(RETROEMU_SDL2_ORIGINE "sources (statique)" CACHE INTERNAL "")
    endif()

endif()
