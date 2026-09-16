// ===========================================================================
//  RetroEmu — entry point
// ===========================================================================
//  STEP 1 of the plan: skeleton. There is no emulation here yet.
//  This file only proves three things, which are the three build-related
//  requirements of the subject (Chapter IV, p.6):
//
//    1. the project builds with a SINGLE command into a SINGLE executable;
//    2. SDL2 is available (system-provided or built automatically);
//    3. a 160x144 texture is displayed on screen, scaled up.
//
//  The test pattern shown here will be replaced by the PPU framebuffer in
//  step 9.
// ===========================================================================

#include <SDL.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "retroemu/core/types.hpp"

namespace {

using retroemu::kScreenHeight;
using retroemu::kScreenPixels;
using retroemu::kScreenWidth;
using retroemu::u32;
using retroemu::u8;

constexpr const char *kVersion = "0.1.0 (step 1: skeleton)";

// ---------------------------------------------------------------------------
//  The framebuffer.
// ---------------------------------------------------------------------------
//  160x144 pixels in ARGB8888: exactly what the PPU will produce in step 9.
//  The frontend will never know anything else about the emulation.
// ---------------------------------------------------------------------------
using Framebuffer = std::array<u32, kScreenPixels>;

constexpr u32 argb(u8 r, u8 g, u8 b)
{
    return 0xFF000000u | (static_cast<u32>(r) << 16) |
           (static_cast<u32>(g) << 8) | static_cast<u32>(b);
}

// The four DMG shades (original greenish LCD palette).
// In step 9 these will come from the BGP register (0xFF47).
constexpr u32 kDmgShades[4] = {
    argb(0x9B, 0xBC, 0x0F),   // index 0: lightest
    argb(0x8B, 0xAC, 0x0F),   // index 1
    argb(0x30, 0x62, 0x30),   // index 2
    argb(0x0F, 0x38, 0x0F),   // index 3: darkest
};

// ---------------------------------------------------------------------------
//  Test pattern.
// ---------------------------------------------------------------------------
//  An 8x8 checkerboard: exactly the size of one hardware tile. If the
//  checkerboard looks sharp and square on screen, then the
//  framebuffer -> texture -> window chain is correct, and whatever the PPU
//  draws later can be trusted to reach the screen unaltered.
// ---------------------------------------------------------------------------
void draw_test_pattern(Framebuffer &fb, int frame)
{
    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            const int tile_x = x / 8;          // 20 tiles across
            const int tile_y = y / 8;          // 18 tiles down
            const bool border = (x < 2 || x >= kScreenWidth - 2 ||
                                 y < 2 || y >= kScreenHeight - 2);

            u8 shade;
            if (border) {
                shade = 3;                                        // dark frame
            } else if (y < 16) {
                shade = static_cast<u8>((x * 4) / kScreenWidth);  // 4-shade ramp
            } else {
                // Scrolling checkerboard: proves the window keeps refreshing.
                shade = static_cast<u8>(((tile_x + tile_y + frame / 30) % 2) ? 1 : 2);
            }
            fb[static_cast<std::size_t>(y) * kScreenWidth + x] = kDmgShades[shade];
        }
    }
}

// ---------------------------------------------------------------------------
//  --selftest mode: verify the pipeline WITHOUT opening a window.
// ---------------------------------------------------------------------------
//  Essential for testing over SSH, inside a container or in CI, where no
//  display server exists. The same mechanism will be reused in step 9 to
//  compare rendered output against the acid2 reference images automatically.
// ---------------------------------------------------------------------------
int run_selftest(const char *ppm_path)
{
    Framebuffer fb{};
    draw_test_pattern(fb, 0);

    // Check 1: the framebuffer matches the hardware screen size.
    if (fb.size() != kScreenPixels) {
        std::fprintf(stderr, "FAILED: wrong framebuffer size\n");
        return 1;
    }

    // Check 2: the top-left pixel belongs to the border (shade 3).
    if (fb[0] != kDmgShades[3]) {
        std::fprintf(stderr, "FAILED: pixel (0,0) = 0x%08X, expected 0x%08X\n",
                     fb[0], kDmgShades[3]);
        return 1;
    }

    // Check 3: all four shades are actually present.
    for (int s = 0; s < 4; ++s) {
        bool found = false;
        for (u32 px : fb) {
            if (px == kDmgShades[s]) { found = true; break; }
        }
        if (!found) {
            std::fprintf(stderr, "FAILED: shade %d missing from framebuffer\n", s);
            return 1;
        }
    }

    // Write a PPM image: a trivial text-header format, readable by any viewer
    // and inspectable by hand.
    if (ppm_path != nullptr) {
        std::FILE *f = std::fopen(ppm_path, "wb");
        if (f == nullptr) {
            std::fprintf(stderr, "FAILED: cannot write %s\n", ppm_path);
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
        std::printf("  image written : %s\n", ppm_path);
    }

    std::printf("  framebuffer   : %dx%d = %zu pixels\n",
                kScreenWidth, kScreenHeight, kScreenPixels);
    std::printf("  SDL2 headers  : %d.%d.%d\n",
                SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    SDL_version linked;
    SDL_GetVersion(&linked);
    std::printf("  SDL2 linked   : %d.%d.%d\n", linked.major, linked.minor, linked.patch);
    std::printf("\033[1;32m  SELFTEST OK\033[0m\n");
    return 0;
}

// ---------------------------------------------------------------------------
//  Graphics loop.
// ---------------------------------------------------------------------------
int run_window(int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "RetroEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kScreenWidth * scale, kScreenHeight * scale, SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // No SDL_RENDERER_PRESENTVSYNC on purpose: decision D6. VSync would lock
    // emulation to the corrector's monitor refresh rate (60 Hz) while the
    // hardware runs at 59.727 Hz. Pacing will be driven by a high-resolution
    // clock in step 11.
    SDL_Renderer *renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Nearest-neighbour filtering: pixels must stay square and sharp.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    // SDL_TEXTUREACCESS_STREAMING: a texture meant to be rewritten every
    // frame, which is exactly what an emulator framebuffer is.
    SDL_Texture *texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, kScreenWidth, kScreenHeight);
    if (texture == nullptr) {
        std::fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::printf("Window %dx%d (scale x%d). Press Escape or close to quit.\n",
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

        // Temporary pacing. Replaced in step 11 by a high-resolution clock
        // locked to 59.727 frames per second.
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
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --selftest [file.ppm]  check the graphics pipeline without a window\n"
        "  --scale N              window magnification factor (default: 4)\n"
        "  --version              print version and exit\n"
        "  --help                 print this help and exit\n",
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
                std::fprintf(stderr, "--scale expects a number\n");
                return 1;
            }
            scale = std::atoi(argv[++i]);
            if (scale < 1 || scale > 16) {
                std::fprintf(stderr, "--scale must be between 1 and 16\n");
                return 1;
            }
            continue;
        }
        std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
        print_usage(argv[0]);
        return 1;
    }

    return run_window(scale);
}
