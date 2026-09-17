#include "retroemu/front/frontend.hpp"

#include <SDL.h>

#include <cstdio>
#include <vector>

#include "retroemu/core/gameboy.hpp"
#include "retroemu/core/joypad.hpp"

namespace retroemu {
namespace {

// ---------------------------------------------------------------------------
//  Keyboard mapping.
// ---------------------------------------------------------------------------
//  The subject names the buttons but says nothing about which keys reach
//  them, so this is a choice. It follows the layout most emulators use, and
//  is the one place to change if another is preferred.
// ---------------------------------------------------------------------------
struct KeyBinding { SDL_Keycode key; Button button; };

const KeyBinding kBindings[] = {
    {SDLK_UP,        ButtonUp},
    {SDLK_DOWN,      ButtonDown},
    {SDLK_LEFT,      ButtonLeft},
    {SDLK_RIGHT,     ButtonRight},
    {SDLK_x,         ButtonA},
    {SDLK_z,         ButtonB},
    {SDLK_RETURN,    ButtonStart},
    {SDLK_BACKSPACE, ButtonSelect},
    {SDLK_RSHIFT,    ButtonSelect},   // a second key for Select, as is common
};

// ---------------------------------------------------------------------------
//  Pacing.
// ---------------------------------------------------------------------------
//  Decision D6: no VSync. Locking to the monitor would tie emulation to
//  whatever refresh rate the corrector's screen happens to have, and the
//  hardware runs at 59.727 frames per second, not 60.
//
//  So a deadline is computed from the hardware's own figures and waited for.
//  SDL_Delay is coarse, a millisecond at best, so it covers the bulk of the
//  wait and a short spin covers the tail. Spinning the whole way would burn a
//  core for nothing.
// ---------------------------------------------------------------------------
class Pacer {
public:
    explicit Pacer(double frames_per_second)
        : frequency_(SDL_GetPerformanceFrequency()),
          period_(static_cast<Uint64>(static_cast<double>(SDL_GetPerformanceFrequency()) /
                                      frames_per_second)),
          deadline_(SDL_GetPerformanceCounter()) {}

    void wait_for_next_frame()
    {
        deadline_ += period_;

        const Uint64 now = SDL_GetPerformanceCounter();
        if (now >= deadline_) {
            // Behind schedule. Start again from now rather than accumulating a
            // debt that would make the emulator sprint to catch up.
            deadline_ = now;
            ++late_frames_;
            return;
        }

        const double remaining_ms =
            1000.0 * static_cast<double>(deadline_ - now) / static_cast<double>(frequency_);
        if (remaining_ms > 2.0) SDL_Delay(static_cast<Uint32>(remaining_ms - 1.0));
        while (SDL_GetPerformanceCounter() < deadline_) { /* the last fraction */ }
    }

    u64 late_frames() const { return late_frames_; }

private:
    Uint64 frequency_;
    Uint64 period_;
    Uint64 deadline_;
    u64    late_frames_ = 0;
};

// The test pattern shown when no cartridge is loaded, so the window is never
// an unexplained black rectangle.
void draw_idle_pattern(std::vector<u32> &pixels, u64 frame)
{
    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            const bool border = (x < 2 || x >= kScreenWidth - 2 ||
                                 y < 2 || y >= kScreenHeight - 2);
            const u8 shade = border ? 3
                           : static_cast<u8>((((x / 8) + (y / 8) + frame / 30) % 2) ? 1 : 2);
            pixels[static_cast<std::size_t>(y) * kScreenWidth + x] = Ppu::dmg_shade(shade);
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
int run_frontend(const FrontendOptions &options)
{
    GameBoy gb;
    if (!options.rom_path.empty()) {
        std::string error;
        if (!gb.load(options.rom_path, options.force_cgb, error)) {
            std::fprintf(stderr, "%s: %s\n", options.rom_path.c_str(), error.c_str());
            return 1;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   // keep pixels square

    SDL_Window *window = SDL_CreateWindow(
        "RetroEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kScreenWidth * options.scale, kScreenHeight * options.scale, SDL_WINDOW_SHOWN);
    // Hardware acceleration first, then software. A corrector running over
    // SSH with X forwarding, inside a virtual machine, or on a system with no
    // GPU driver has no accelerated renderer, and refusing to start there
    // would be a poor reason to fail an evaluation.
    SDL_Renderer *renderer = nullptr;
    if (window != nullptr) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (renderer == nullptr) {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            if (renderer != nullptr)
                std::printf("No accelerated renderer available; using software rendering.\n");
        }
    }
    SDL_Texture  *texture  = renderer
        ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING, kScreenWidth, kScreenHeight)
        : nullptr;

    if (texture == nullptr) {
        std::fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window)   SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::printf("Window %dx%d (scale x%d)\n",
                kScreenWidth * options.scale, kScreenHeight * options.scale, options.scale);
    std::printf("Controls: arrows, X = A, Z = B, Enter = Start, Backspace = Select\n");
    std::printf("          Space = pause/resume, Escape = quit\n");

    std::vector<u32> idle(kScreenPixels, 0);
    const double frames_per_second =
        static_cast<double>(kSystemClockHz) / static_cast<double>(kTCyclesPerFrame);
    Pacer pacer(frames_per_second);

    bool running = true;
    bool paused  = options.start_paused;
    u64  frames  = 0;

    // The title is refreshed once a second with the measured rate, which is
    // how the "normal speed" of section V.3 is checked at a glance.
    Uint64 fps_window_start = SDL_GetPerformanceCounter();
    u64    fps_window_frames = 0;

    while (running) {
        // --- Events. Pumped every frame, which is what keeps the interface
        //     responsive while the emulation runs (subject V.3).
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) { running = false; continue; }
            if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) continue;

            const bool down = (event.type == SDL_KEYDOWN);

            if (down && event.key.keysym.sym == SDLK_ESCAPE) { running = false; continue; }
            if (down && !event.key.repeat && event.key.keysym.sym == SDLK_SPACE) {
                paused = !paused;
                std::printf("%s\n", paused ? "paused" : "resumed");
                continue;
            }

            for (const KeyBinding &binding : kBindings) {
                if (binding.key == event.key.keysym.sym) gb.bus().joypad().set(binding.button, down);
            }
        }

        // --- Emulate one frame ------------------------------------------------
        if (!paused && gb.loaded()) gb.run_frame();

        // --- Present ----------------------------------------------------------
        const u32 *pixels;
        if (gb.loaded()) {
            pixels = gb.bus().ppu().framebuffer().data();
        } else {
            draw_idle_pattern(idle, frames);
            pixels = idle.data();
        }
        SDL_UpdateTexture(texture, nullptr, pixels, kScreenWidth * static_cast<int>(sizeof(u32)));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        ++frames;
        ++fps_window_frames;

        // --- Title, once a second --------------------------------------------
        const Uint64 now = SDL_GetPerformanceCounter();
        const double elapsed =
            static_cast<double>(now - fps_window_start) / static_cast<double>(SDL_GetPerformanceFrequency());
        if (elapsed >= 1.0) {
            char title[128];
            std::snprintf(title, sizeof(title), "RetroEmu - %.1f fps%s%s",
                          static_cast<double>(fps_window_frames) / elapsed,
                          paused ? " - paused" : "",
                          gb.loaded() ? "" : " - no cartridge");
            SDL_SetWindowTitle(window, title);
            fps_window_start  = now;
            fps_window_frames = 0;
        }

        if (options.frame_limit != 0 && frames >= options.frame_limit) running = false;

        pacer.wait_for_next_frame();
    }

    if (options.frame_limit != 0) {
        std::printf("  frames presented : %llu\n", static_cast<unsigned long long>(frames));
        std::printf("  frames late      : %llu\n",
                    static_cast<unsigned long long>(pacer.late_frames()));
        if (gb.loaded()) {
            std::printf("  emulated time    : %.3f s\n",
                        static_cast<double>(gb.bus().clock().t_sys()) / kSystemClockHz);
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace retroemu
