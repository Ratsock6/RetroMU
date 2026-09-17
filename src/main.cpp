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
#include <vector>

#include "retroemu/core/bus.hpp"
#include "retroemu/core/cartridge.hpp"
#include "retroemu/core/cpu.hpp"
#include "retroemu/debug/cpu_selftest.hpp"
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
int run_cartridge_report(const std::vector<std::string> &paths, bool compact)
{
    if (paths.empty()) {
        std::fprintf(stderr, "expected at least one ROM file\n");
        return 1;
    }

    int failures = 0;
    if (compact) print_list_header();

    for (std::size_t i = 0; i < paths.size(); ++i) {
        retroemu::Cartridge cart;
        std::string error;

        if (!cart.load_from_file(paths[i], error)) {
            std::fprintf(stderr, "%s: %s\n", paths[i].c_str(), error.c_str());
            ++failures;
            continue;
        }

        if (compact) {
            print_cartridge_row(cart);
        } else {
            if (i > 0) std::printf("\n%s\n\n", std::string(60, '-').c_str());
            print_cartridge_info(cart);
        }
    }
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  Memory map walk (step 3).
// ---------------------------------------------------------------------------
//  Proves three things at once:
//    - every address is routed to the right owner;
//    - every access costs exactly 4 T-cycles, charged as it happens;
//    - the CPU and system clock domains diverge in double-speed mode.
// ---------------------------------------------------------------------------
void probe(retroemu::Bus &bus, retroemu::u16 addr, const char *note)
{
    const retroemu::u8 before = bus.read(addr);

    // Try to write the complement and read it back: that tells us whether the
    // region is writable, without needing a hard-coded list.
    const retroemu::u8 probe_value = static_cast<retroemu::u8>(~before);
    bus.write(addr, probe_value);
    const retroemu::u8 after = bus.read(addr);
    const bool writable = (after == probe_value);
    bus.write(addr, before);   // put it back

    std::printf("  0x%04X  %-16s 0x%02X   %-9s %s\n",
                addr, retroemu::to_string(retroemu::region_of(addr)), before,
                writable ? "writable" : "read-only", note);
}

int run_memtest(const char *rom_path, bool force_cgb)
{
    retroemu::Cartridge cart;
    std::string error;
    if (!cart.load_from_file(rom_path, error)) {
        std::fprintf(stderr, "%s: %s\n", rom_path, error.c_str());
        return 1;
    }

    // The model comes from the cartridge header unless it is forced.
    const bool cgb_cart = cart.header().cgb != retroemu::CgbSupport::None;
    const retroemu::Model model =
        (force_cgb || cgb_cart) ? retroemu::Model::Cgb : retroemu::Model::Dmg;

    retroemu::Bus bus;
    bus.attach(std::move(cart), model);

    std::printf("Memory map walk: %s\n", rom_path);
    std::printf("Model          : %s\n\n", bus.model_name());

    std::printf("  ADDR    REGION           READ   ACCESS    NOTE\n");
    std::printf("  ------  ---------------- -----  --------- ----------------------------\n");
    probe(bus, 0x0000, "cartridge header area");
    probe(bus, 0x0100, "entry point");
    probe(bus, 0x2000, "write here = bank switch command");
    probe(bus, 0x4000, "switchable ROM bank");
    probe(bus, 0x8000, "VRAM, owned by the PPU");
    probe(bus, 0xA000, "external RAM (cartridge)");
    probe(bus, 0xC000, "WRAM bank 0");
    probe(bus, 0xD000, "WRAM bank N");
    probe(bus, 0xFE00, "OAM, owned by the PPU");
    probe(bus, 0xFEA0, "unusable region");
    probe(bus, 0xFF40, "I/O register (LCDC)");
    probe(bus, 0xFF80, "HRAM");
    probe(bus, 0xFFFF, "interrupt enable");

    // --- Echo RAM ----------------------------------------------------------
    std::printf("\nEcho RAM (0xE000-0xFDFF mirrors 0xC000-0xDDFF)\n");
    bus.write(0xC000, 0x42);
    const retroemu::u8 echoed = bus.read(0xE000);
    std::printf("  wrote 0x42 at 0xC000, read 0x%02X at 0xE000   %s\n",
                echoed, echoed == 0x42 ? "mirrored" : "NOT MIRRORED");
    bus.write(0xE001, 0x99);
    const retroemu::u8 back = bus.read(0xC001);
    std::printf("  wrote 0x99 at 0xE001, read 0x%02X at 0xC001   %s\n",
                back, back == 0x99 ? "mirrored" : "NOT MIRRORED");

    // --- Writing into ROM ---------------------------------------------------
    std::printf("\nWriting into ROM (0x0000-0x7FFF)\n");
    const retroemu::u8 rom_before = bus.read(0x0000);
    bus.write(0x2000, 0x05);
    std::printf("  0x0000 still reads 0x%02X (unchanged), and the cartridge\n", rom_before);
    std::printf("  recorded %llu bank-switch command(s), ignored until step 13\n",
                static_cast<unsigned long long>(bus.cartridge().bank_commands_seen()));

    // --- Clock accounting ---------------------------------------------------
    const retroemu::u64 accesses = bus.access_count();
    const retroemu::u64 t_cpu    = bus.clock().t_cpu();
    std::printf("\nClock accounting\n");
    std::printf("  accesses performed : %llu\n", static_cast<unsigned long long>(accesses));
    std::printf("  t_cpu elapsed      : %llu\n", static_cast<unsigned long long>(t_cpu));
    std::printf("  cycles per access  : %llu   %s\n",
                static_cast<unsigned long long>(accesses ? t_cpu / accesses : 0),
                (accesses && t_cpu == accesses * 4) ? "(exactly 4, as the hardware charges)" : "(UNEXPECTED)");

    // peek() must be free and invisible.
    const retroemu::u64 before_peek = bus.clock().t_cpu();
    for (int i = 0; i < 1000; ++i) (void)bus.peek(static_cast<retroemu::u16>(i));
    std::printf("  1000 peek() calls  : t_cpu moved by %llu   %s\n",
                static_cast<unsigned long long>(bus.clock().t_cpu() - before_peek),
                bus.clock().t_cpu() == before_peek ? "(free, as the debugger needs)" : "(UNEXPECTED)");

    // --- Double speed -------------------------------------------------------
    // The whole point of decision D8: on CGB the CPU can run twice as fast
    // while the PPU keeps its own pace.
    std::printf("\nDouble-speed mode (CGB, subject V.6)\n");
    const retroemu::u64 cpu0 = bus.clock().t_cpu();
    const retroemu::u64 sys0 = bus.clock().t_sys();
    for (int i = 0; i < 10; ++i) (void)bus.read(0xC000);
    std::printf("  normal speed, 10 accesses : t_cpu +%llu, t_sys +%llu\n",
                static_cast<unsigned long long>(bus.clock().t_cpu() - cpu0),
                static_cast<unsigned long long>(bus.clock().t_sys() - sys0));

    bus.set_double_speed(true);
    const retroemu::u64 cpu1 = bus.clock().t_cpu();
    const retroemu::u64 sys1 = bus.clock().t_sys();
    for (int i = 0; i < 10; ++i) (void)bus.read(0xC000);
    const retroemu::u64 dcpu = bus.clock().t_cpu() - cpu1;
    const retroemu::u64 dsys = bus.clock().t_sys() - sys1;
    std::printf("  double speed, 10 accesses : t_cpu +%llu, t_sys +%llu   %s\n",
                static_cast<unsigned long long>(dcpu),
                static_cast<unsigned long long>(dsys),
                dsys * 2 == dcpu ? "(the PPU sees half: correct)" : "(UNEXPECTED)");
    std::printf("  PPU cycles received       : %llu = t_sys total (%llu)   %s\n",
                static_cast<unsigned long long>(bus.ppu().elapsed()),
                static_cast<unsigned long long>(bus.clock().t_sys()),
                bus.ppu().elapsed() == bus.clock().t_sys() ? "(in sync)" : "(UNEXPECTED)");

    return 0;
}

// ---------------------------------------------------------------------------
//  Headless execution (step 4).
// ---------------------------------------------------------------------------
//  Runs a ROM with no window at all and prints whatever the game sends over
//  the link port. blargg's CPU tests report through that port, which is how
//  the instruction set can be validated before any screen exists.
// ---------------------------------------------------------------------------
int run_rom(const char *rom_path, bool force_cgb, retroemu::u64 max_cycles, bool verbose)
{
    retroemu::Cartridge cart;
    std::string error;
    if (!cart.load_from_file(rom_path, error)) {
        std::fprintf(stderr, "%s: %s\n", rom_path, error.c_str());
        return 1;
    }

    const bool cgb_cart = cart.header().cgb != retroemu::CgbSupport::None;
    const retroemu::Model model =
        (force_cgb || cart.header().cgb == retroemu::CgbSupport::Only)
            ? retroemu::Model::Cgb : retroemu::Model::Dmg;
    (void)cgb_cart;

    retroemu::Bus bus;
    bus.attach(std::move(cart), model);
    retroemu::Cpu cpu;
    cpu.reset(model);

    if (verbose) {
        std::printf("Running %s in %s mode, up to %llu cycles\n\n",
                    rom_path, bus.model_name(),
                    static_cast<unsigned long long>(max_cycles));
    }

    std::size_t printed = 0;
    retroemu::u64 instructions = 0;

    while (bus.clock().t_cpu() < max_cycles) {
        cpu.step(bus);
        ++instructions;

        // Echo serial output as it appears, so a hanging test still shows
        // how far it got.
        const std::string &out = bus.serial_output();
        while (printed < out.size()) std::fputc(out[printed++], stdout);
        std::fflush(stdout);

        // blargg's runtime prints a verdict when the test ends; there is no
        // point burning the rest of the cycle budget after that.
        if (out.size() >= 6) {
            const std::size_t tail = out.size() < 64 ? 0 : out.size() - 64;
            const std::string end  = out.substr(tail);
            if (end.find("Passed") != std::string::npos ||
                end.find("Failed") != std::string::npos) break;
        }

        if (cpu.illegal()) {
            std::fprintf(stderr, "\nillegal opcode 0x%02X at PC=0x%04X\n",
                         cpu.illegal_opcode(), cpu.regs().pc);
            return 1;
        }
        if (cpu.stopped()) break;
    }

    const std::string &out = bus.serial_output();

    if (verbose) {
        std::printf("\n\n");
        std::printf("instructions executed : %llu\n", static_cast<unsigned long long>(instructions));
        std::printf("T-cycles elapsed      : %llu\n", static_cast<unsigned long long>(bus.clock().t_cpu()));
        std::printf("emulated time         : %.2f s\n",
                    static_cast<double>(bus.clock().t_cpu()) / retroemu::kSystemClockHz);
        std::printf("final PC              : 0x%04X\n", cpu.regs().pc);
        std::printf("serial bytes received : %zu\n", out.size());
    }

    // blargg's runtime prints "Passed" or "Failed" when a test completes.
    if (out.find("Passed") != std::string::npos) return 0;
    if (out.find("Failed") != std::string::npos) return 2;
    return 3;   // no verdict: the ROM never finished
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
        "  --memtest <rom>        walk the memory map and check the clock\n"
        "  --run <rom>            run headless, print the serial output\n"
        "  --max-cycles N         cycle budget for --run (default 250000000)\n"
        "  --quiet                with --run, print only the serial output\n"
        "  --cpucheck             run the built-in CPU self-test\n"
        "  --cgb                  force CGB mode (used with --memtest)\n"
        "  --selftest [file.ppm]  check the graphics pipeline without a window\n"
        "  --scale N              window magnification factor (default: 4)\n"
        "  --version              print version and exit\n"
        "  --help                 print this help and exit\n",
        kVersion, prog);
}

}  // namespace

int main(int argc, char *argv[])
{
    // Options are collected first and the action is executed afterwards, so
    // that flags work wherever they appear on the command line.
    int           scale      = 4;
    bool          force_cgb  = false;
    bool          quiet      = false;
    retroemu::u64 max_cycles = 250000000ULL;   // about 60 emulated seconds

    std::string              action;
    std::vector<std::string> files;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
        if (arg == "--version") { std::printf("RetroEmu %s\n", kVersion); return 0; }

        if (arg == "--cgb")   { force_cgb = true; continue; }
        if (arg == "--quiet") { quiet = true; continue; }

        if (arg == "--scale") {
            if (i + 1 >= argc) { std::fprintf(stderr, "--scale expects a number\n"); return 1; }
            scale = std::atoi(argv[++i]);
            if (scale < 1 || scale > 16) {
                std::fprintf(stderr, "--scale must be between 1 and 16\n");
                return 1;
            }
            continue;
        }
        if (arg == "--max-cycles") {
            if (i + 1 >= argc) { std::fprintf(stderr, "--max-cycles expects a number\n"); return 1; }
            max_cycles = std::strtoull(argv[++i], nullptr, 10);
            if (max_cycles == 0) {
                std::fprintf(stderr, "--max-cycles must be greater than zero\n");
                return 1;
            }
            continue;
        }

        if (arg == "--selftest" || arg == "--info" || arg == "--list" ||
            arg == "--memtest"  || arg == "--run"  || arg == "--cpucheck") {
            if (!action.empty()) {
                std::fprintf(stderr, "%s and %s cannot be combined\n",
                             action.c_str(), arg.c_str());
                return 1;
            }
            action = arg;
            continue;
        }

        if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
        files.push_back(arg);
    }

    if (action.empty())        return run_window(scale);
    if (action == "--selftest") return run_selftest(files.empty() ? nullptr : files[0].c_str());
    if (action == "--info")     return run_cartridge_report(files, /*compact=*/false);
    if (action == "--list")     return run_cartridge_report(files, /*compact=*/true);
    if (action == "--cpucheck") return retroemu::run_cpu_selftest(!quiet) == 0 ? 0 : 1;

    // The remaining actions take exactly one ROM.
    if (files.empty()) {
        std::fprintf(stderr, "%s expects a ROM file\n", action.c_str());
        return 1;
    }
    if (action == "--memtest") return run_memtest(files[0].c_str(), force_cgb);
    return run_rom(files[0].c_str(), force_cgb, max_cycles, !quiet);
}
