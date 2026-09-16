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

#include "retroemu/core/cartridge.hpp"
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

// ---------------------------------------------------------------------------
//  Cartridge report (step 2).
// ---------------------------------------------------------------------------
const char *yes_no(bool b) { return b ? "yes" : "no"; }

std::string human_size(std::size_t bytes)
{
    char buf[64];
    if (bytes == 0)               std::snprintf(buf, sizeof(buf), "none");
    else if (bytes < 1024)        std::snprintf(buf, sizeof(buf), "%zu bytes", bytes);
    else if (bytes < 1024 * 1024) std::snprintf(buf, sizeof(buf), "%zu KiB", bytes / 1024);
    else                          std::snprintf(buf, sizeof(buf), "%zu MiB", bytes / (1024 * 1024));
    return buf;
}

const char *cgb_short(retroemu::CgbSupport c)
{
    switch (c) {
        case retroemu::CgbSupport::None:     return "no";
        case retroemu::CgbSupport::Enhanced: return "enhanced";
        case retroemu::CgbSupport::Only:     return "only";
    }
    return "?";
}

// Detailed block, one cartridge per call.
void print_cartridge_info(const retroemu::Cartridge &cart)
{
    const retroemu::CartridgeHeader &h = cart.header();

    std::printf("File            : %s\n", cart.path().c_str());
    std::printf("File size       : %zu bytes (%s)\n",
                cart.rom().size(), human_size(cart.rom().size()).c_str());
    std::printf("\n");

    std::printf("Title           : %s\n", h.title.empty() ? "(none)" : h.title.c_str());
    if (!h.manufacturer_code.empty())
        std::printf("Manufacturer    : %s\n", h.manufacturer_code.c_str());
    std::printf("Licensee        : %s\n",
                h.licensee_code.empty() ? "(none)" : h.licensee_code.c_str());
    std::printf("CGB support     : 0x%02X  %s\n",
                h.cgb == retroemu::CgbSupport::Only     ? 0xC0 :
                h.cgb == retroemu::CgbSupport::Enhanced ? 0x80 : 0x00,
                retroemu::to_string(h.cgb));
    std::printf("SGB support     : %s\n", yes_no(h.sgb));
    std::printf("Destination     : %s\n", h.destination_code == 0x00 ? "Japan" : "overseas");
    std::printf("ROM version     : %u\n", h.rom_version);
    std::printf("\n");

    std::printf("Cartridge type  : 0x%02X  %s\n",
                h.type_code, retroemu::cartridge_type_name(h.type_code));
    const char *mbc_note = retroemu::is_mandatory_mbc(h.mbc)      ? ""
                         : h.mbc == retroemu::MbcType::Unknown     ? "   (unsupported)"
                         : h.mbc == retroemu::MbcType::Mbc3        ? "   (bonus part of the subject)"
                                                                   : "   (not required by the subject)";
    std::printf("  MBC           : %s%s\n", retroemu::to_string(h.mbc), mbc_note);
    std::printf("  External RAM  : %s\n", yes_no(h.has_ram));
    std::printf("  Battery       : %s\n", yes_no(h.has_battery));
    std::printf("  RTC           : %s\n", yes_no(h.has_timer));
    std::printf("  Rumble        : %s\n", yes_no(h.has_rumble));
    std::printf("\n");

    std::printf("ROM size        : 0x%02X  %s (%u banks of 16 KiB)\n",
                h.rom_size_code, human_size(h.rom_size).c_str(), h.rom_banks);
    if (h.mbc == retroemu::MbcType::Mbc2)
        std::printf("RAM size        : 0x%02X  512 x 4 bits, built into the MBC2 chip\n",
                    h.ram_size_code);
    else
        std::printf("RAM size        : 0x%02X  %s%s\n",
                    h.ram_size_code, human_size(h.ram_size).c_str(),
                    h.ram_banks > 0 ? "" : "");
    std::printf("\n");

    std::printf("Header checksum : 0x%02X  (computed 0x%02X)  %s\n",
                h.header_checksum, h.computed_header_checksum,
                h.header_checksum_valid() ? "ok" : "MISMATCH");
    std::printf("Global checksum : 0x%04X (computed 0x%04X)  %s\n",
                h.global_checksum, h.computed_global_checksum,
                h.global_checksum_valid() ? "ok" : "mismatch (never checked by hardware)");

    if (!h.warnings.empty()) {
        std::printf("\nWarnings:\n");
        for (const std::string &w : h.warnings) std::printf("  - %s\n", w.c_str());
    }
}

void print_list_header()
{
    std::printf("%-42s %-16s %-9s %-24s %-9s %-9s %s\n",
                "FILE", "TITLE", "CGB", "CARTRIDGE TYPE", "ROM", "RAM", "HDR");
    std::printf("%-42s %-16s %-9s %-24s %-9s %-9s %s\n",
                "------------------------------------------", "----------------", "---------",
                "------------------------", "---------", "---------", "---");
}

// One compact row per cartridge.
void print_cartridge_row(const retroemu::Cartridge &cart)
{
    const retroemu::CartridgeHeader &h = cart.header();
    const std::string ram = (h.mbc == retroemu::MbcType::Mbc2)
                                ? std::string("512x4b")
                                : human_size(h.ram_size);

    std::printf("%-42s %-16s %-9s %-24s %-9s %-9s %s\n",
                cart.path().c_str(),
                h.title.empty() ? "-" : h.title.c_str(),
                cgb_short(h.cgb),
                retroemu::cartridge_type_name(h.type_code),
                human_size(h.rom_size).c_str(),
                ram.c_str(),
                h.header_checksum_valid() ? "ok" : "BAD");
}

// Shared driver for --info and --list. Returns a process exit code.
int run_cartridge_report(char *argv[], int first, int argc, bool compact)
{
    if (first >= argc) {
        std::fprintf(stderr, "expected at least one ROM file\n");
        return 1;
    }

    int failures = 0;
    if (compact) print_list_header();

    for (int i = first; i < argc; ++i) {
        retroemu::Cartridge cart;
        std::string error;

        if (!cart.load_from_file(argv[i], error)) {
            std::fprintf(stderr, "%s: %s\n", argv[i], error.c_str());
            ++failures;
            continue;
        }

        if (compact) {
            print_cartridge_row(cart);
        } else {
            if (i > first) std::printf("\n%s\n\n", std::string(60, '-').c_str());
            print_cartridge_info(cart);
        }
    }
    return failures == 0 ? 0 : 1;
}

void print_usage(const char *prog)
{
    std::printf(
        "RetroEmu %s\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --info <rom>...        print the cartridge header of each ROM\n"
        "  --list <rom>...        print one summary line per ROM\n"
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
        if (arg == "--info") {
            return run_cartridge_report(argv, i + 1, argc, /*compact=*/false);
        }
        if (arg == "--list") {
            return run_cartridge_report(argv, i + 1, argc, /*compact=*/true);
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
