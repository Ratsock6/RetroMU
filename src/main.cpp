// ===========================================================================
//  RetroEmu — point d'entree
// ===========================================================================
//  ETAPE 1 du plan : squelette. Il n'y a encore AUCUNE emulation ici.
//  Ce fichier prouve seulement trois choses, qui sont les trois exigences
//  du Chapitre IV du sujet (p.6) :
//
//    1. le projet compile avec UNE SEULE commande et produit UN exécutable ;
//    2. SDL2 est disponible (systeme ou compile automatiquement) ;
//    3. une texture 160x144 s'affiche a l'ecran, mise a l'echelle.
//
//  La mire affichee sera remplacee par le framebuffer du PPU a l'etape 9.
// ===========================================================================

#include <SDL.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "retroemu/core/types.hpp"

namespace {

using retroemu::kScreenHeight;
using retroemu::kScreenPixels;
using retroemu::kScreenWidth;
using retroemu::u32;
using retroemu::u8;

constexpr const char *kVersion = "0.1.0 (etape 1 : squelette)";

// ---------------------------------------------------------------------------
//  Le framebuffer.
// ---------------------------------------------------------------------------
//  160x144 pixels en ARGB8888 : c'est exactement ce que le PPU produira a
//  l'etape 9. Le frontend ne saura jamais rien d'autre de l'emulation.
// ---------------------------------------------------------------------------
using Framebuffer = std::array<u32, kScreenPixels>;

constexpr u32 argb(u8 r, u8 g, u8 b)
{
    return 0xFF000000u | (static_cast<u32>(r) << 16) |
           (static_cast<u32>(g) << 8) | static_cast<u32>(b);
}

// Les 4 nuances de la DMG (palette verdatre d'origine).
// A l'etape 9 elles viendront du registre BGP (0xFF47).
constexpr u32 kDmgShades[4] = {
    argb(0x9B, 0xBC, 0x0F),   // index 0 : le plus clair
    argb(0x8B, 0xAC, 0x0F),   // index 1
    argb(0x30, 0x62, 0x30),   // index 2
    argb(0x0F, 0x38, 0x0F),   // index 3 : le plus fonce
};

// ---------------------------------------------------------------------------
//  Mire de test.
// ---------------------------------------------------------------------------
//  On dessine un damier de 8x8 pixels : c'est exactement la taille d'une tile
//  du materiel. Si le damier est net et carre a l'ecran, la chaine
//  framebuffer -> texture -> fenetre est correcte, et tu pourras faire
//  confiance a ce que le PPU affichera plus tard.
// ---------------------------------------------------------------------------
void draw_test_pattern(Framebuffer &fb, int frame)
{
    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            const int tile_x = x / 8;          // 20 tiles de large
            const int tile_y = y / 8;          // 18 tiles de haut
            const bool border = (x < 2 || x >= kScreenWidth - 2 ||
                                 y < 2 || y >= kScreenHeight - 2);

            u8 shade;
            if (border) {
                shade = 3;                                   // cadre fonce
            } else if (y < 16) {
                shade = static_cast<u8>((x * 4) / kScreenWidth);  // degrade des 4 nuances
            } else {
                // Damier qui defile : prouve que la fenetre se rafraichit.
                shade = static_cast<u8>(((tile_x + tile_y + frame / 30) % 2) ? 1 : 2);
            }
            fb[static_cast<std::size_t>(y) * kScreenWidth + x] = kDmgShades[shade];
        }
    }
}

// ---------------------------------------------------------------------------
//  Mode --selftest : verifie la chaine SANS ouvrir de fenetre.
// ---------------------------------------------------------------------------
//  Indispensable pour tester en SSH, en conteneur ou en integration continue,
//  la ou il n'y a aucun serveur graphique. Ce mecanisme resservira a l'etape 9
//  pour comparer automatiquement le rendu aux ROMs de test.
// ---------------------------------------------------------------------------
int run_selftest(const char *ppm_path)
{
    Framebuffer fb{};
    draw_test_pattern(fb, 0);

    // Verification 1 : la taille du framebuffer est bien celle du materiel.
    if (fb.size() != kScreenPixels) {
        std::fprintf(stderr, "ECHEC : taille du framebuffer incorrecte\n");
        return 1;
    }

    // Verification 2 : le coin haut-gauche appartient au cadre (nuance 3).
    if (fb[0] != kDmgShades[3]) {
        std::fprintf(stderr, "ECHEC : pixel (0,0) = 0x%08X, attendu 0x%08X\n",
                     fb[0], kDmgShades[3]);
        return 1;
    }

    // Verification 3 : les 4 nuances sont bien toutes presentes.
    for (int s = 0; s < 4; ++s) {
        bool trouve = false;
        for (u32 px : fb) {
            if (px == kDmgShades[s]) { trouve = true; break; }
        }
        if (!trouve) {
            std::fprintf(stderr, "ECHEC : nuance %d absente du framebuffer\n", s);
            return 1;
        }
    }

    // Ecriture d'une image PPM : format texte trivial, lisible par tout
    // visualiseur, et surtout inspectable a la main avec un editeur.
    if (ppm_path != nullptr) {
        std::FILE *f = std::fopen(ppm_path, "wb");
        if (f == nullptr) {
            std::fprintf(stderr, "ECHEC : impossible d'ecrire %s\n", ppm_path);
            return 1;
        }
        std::fprintf(f, "P6\n%d %d\n255\n", kScreenWidth, kScreenHeight);
        for (u32 px : fb) {
            const unsigned char rgb[3] = {
                static_cast<unsigned char>((px >> 16) & 0xFF),
                static_cast<unsigned char>((px >> 8) & 0xFF),
                static_cast<unsigned char>(px & 0xFF),
            };
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
        std::printf("  image ecrite : %s\n", ppm_path);
    }

    std::printf("  framebuffer   : %dx%d = %zu pixels\n",
                kScreenWidth, kScreenHeight, kScreenPixels);
    std::printf("  SDL2 compile  : %d.%d.%d\n",
                SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    SDL_version lie;
    SDL_GetVersion(&lie);
    std::printf("  SDL2 lie      : %d.%d.%d\n", lie.major, lie.minor, lie.patch);
    std::printf("\033[1;32m  SELFTEST OK\033[0m\n");
    return 0;
}

// ---------------------------------------------------------------------------
//  Boucle graphique.
// ---------------------------------------------------------------------------
int run_window(int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init : %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "RetroEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kScreenWidth * scale, kScreenHeight * scale, SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow : %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Pas de SDL_RENDERER_PRESENTVSYNC : decision D6 du plan. Le VSync
    // verrouillerait l'emulation sur le taux de rafraichissement de l'ecran
    // (60 Hz) alors que le materiel tourne a 59,727 Hz. La cadence sera
    // pilotee par une horloge a l'etape 11.
    SDL_Renderer *renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer : %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Filtrage au plus proche : les pixels doivent rester carres et nets.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    // SDL_TEXTUREACCESS_STREAMING : texture destinee a etre reecrite a chaque
    // image, ce qui est exactement le cas d'un framebuffer d'emulateur.
    SDL_Texture *texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, kScreenWidth, kScreenHeight);
    if (texture == nullptr) {
        std::fprintf(stderr, "SDL_CreateTexture : %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::printf("Fenetre %dx%d (echelle x%d). Echap ou fermeture pour quitter.\n",
                kScreenWidth * scale, kScreenHeight * scale, scale);

    Framebuffer fb{};
    bool running = true;
    int frame = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        draw_test_pattern(fb, frame++);

        SDL_UpdateTexture(texture, nullptr, fb.data(),
                          kScreenWidth * static_cast<int>(sizeof(u32)));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        // Cadencage provisoire. Sera remplace a l'etape 11 par une horloge
        // haute resolution calee sur 59,727 images/seconde.
        SDL_Delay(16);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

void print_usage(const char *prog)
{
    std::printf(
        "RetroEmu %s\n"
        "\n"
        "Usage : %s [options]\n"
        "\n"
        "Options :\n"
        "  --selftest [fichier.ppm]  verifie la chaine graphique sans ouvrir de fenetre\n"
        "  --scale N                 facteur d'agrandissement de la fenetre (defaut : 4)\n"
        "  --version                 affiche la version\n"
        "  --help                    affiche cette aide\n",
        kVersion, prog);
}

}  // namespace

int main(int argc, char *argv[])
{
    int scale = 4;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            std::printf("RetroEmu %s\n", kVersion);
            return 0;
        }
        if (arg == "--selftest") {
            const char *ppm = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : nullptr;
            return run_selftest(ppm);
        }
        if (arg == "--scale") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--scale attend un nombre\n");
                return 1;
            }
            scale = std::atoi(argv[++i]);
            if (scale < 1 || scale > 16) {
                std::fprintf(stderr, "--scale doit etre compris entre 1 et 16\n");
                return 1;
            }
            continue;
        }
        std::fprintf(stderr, "Option inconnue : %s\n", arg.c_str());
        print_usage(argv[0]);
        return 1;
    }

    return run_window(scale);
}
