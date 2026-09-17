#include "retroemu/core/ppu.hpp"

#include "retroemu/core/bus.hpp"   // for Model

namespace retroemu {
namespace {

// STAT bits 3-6 each enable one interrupt source.
constexpr u8 kStatHBlankInt  = 0x08;
constexpr u8 kStatVBlankInt  = 0x10;
constexpr u8 kStatOamInt     = 0x20;
constexpr u8 kStatLycInt     = 0x40;

}  // namespace

const char *to_string(PpuMode mode)
{
    switch (mode) {
        case PpuMode::HBlank:  return "0 HBlank";
        case PpuMode::VBlank:  return "1 VBlank";
        case PpuMode::OamScan: return "2 OAM scan";
        case PpuMode::Drawing: return "3 drawing";
    }
    return "?";
}

void Ppu::reset(Model model)
{
    (void)model;
    vram_.fill(0);
    oam_.fill(0);

    // Values the boot sequence leaves behind: the screen is already on, with
    // the background enabled.
    lcdc_ = 0x91;
    stat_ = 0x85;
    scy_  = 0;
    scx_  = 0;
    ly_   = 0;
    lyc_  = 0;
    bgp_  = 0xFC;
    obp0_ = 0xFF;
    obp1_ = 0xFF;
    wy_   = 0;
    wx_   = 0;

    mode_        = PpuMode::OamScan;
    dot_         = 0;
    stat_line_   = false;
    vblank_irq_  = false;
    stat_irq_    = false;
    frame_ready_ = false;
    vram_bank_   = 0;
    window_line_ = 0;
    framebuffer_.fill(dmg_shade(0));
    elapsed_     = 0;
    frames_      = 0;
}

// ---------------------------------------------------------------------------
//  Memory
// ---------------------------------------------------------------------------
//  NOTE: on real hardware VRAM is unreadable during mode 3 and OAM during
//  modes 2 and 3, returning 0xFF instead. That blocking is deliberately not
//  implemented: no ROM in the bundle tests it, and being permissive can only
//  make a well-behaved game work, never break it. See docs/decisions.md.
// ---------------------------------------------------------------------------
u8 Ppu::read_vram(u16 addr) const
{
    return vram_[vram_bank_ * kVramBankSize + static_cast<std::size_t>(addr - 0x8000)];
}

void Ppu::write_vram(u16 addr, u8 value)
{
    vram_[vram_bank_ * kVramBankSize + static_cast<std::size_t>(addr - 0x8000)] = value;
}

u8 Ppu::read_oam(u16 addr) const
{
    return oam_[static_cast<std::size_t>(addr - 0xFE00)];
}

void Ppu::write_oam(u16 addr, u8 value)
{
    oam_[static_cast<std::size_t>(addr - 0xFE00)] = value;
}

u8 Ppu::vram_bank_register() const { return static_cast<u8>(0xFE | vram_bank_); }
void Ppu::set_vram_bank_register(u8 value) { vram_bank_ = value & 0x01; }

// ---------------------------------------------------------------------------
//  Registers
// ---------------------------------------------------------------------------
u8 Ppu::read(u16 addr) const
{
    switch (addr) {
        case 0xFF40: return lcdc_;
        case 0xFF41: {
            // Bit 7 is not wired and reads as 1. Bits 0-2 are produced by the
            // PPU itself, not by what was written: the current mode, and
            // whether LY currently equals LYC.
            const u8 lyc_flag = (ly_ == lyc_) ? 0x04 : 0x00;
            const u8 mode     = lcd_on() ? static_cast<u8>(mode_) : 0;
            return static_cast<u8>(0x80 | (stat_ & 0x78) | lyc_flag | mode);
        }
        case 0xFF42: return scy_;
        case 0xFF43: return scx_;
        case 0xFF44: return lcd_on() ? ly_ : 0;   // reads 0 while the screen is off
        case 0xFF45: return lyc_;
        case 0xFF47: return bgp_;
        case 0xFF48: return obp0_;
        case 0xFF49: return obp1_;
        case 0xFF4A: return wy_;
        case 0xFF4B: return wx_;
        default:     return 0xFF;
    }
}

void Ppu::write(u16 addr, u8 value)
{
    switch (addr) {
        case 0xFF40: {
            const bool was_on = lcd_on();
            lcdc_ = value;
            const bool now_on = lcd_on();

            if (was_on && !now_on) {
                // Turning the screen off resets the sweep. LY reads 0 and the
                // PPU stops entirely; games do this before rewriting VRAM in
                // bulk, because it is the only moment nothing is being drawn.
                ly_   = 0;
                dot_  = 0;
                mode_ = PpuMode::HBlank;
                stat_line_   = false;
                window_line_ = 0;
                framebuffer_.fill(dmg_shade(0));   // a dark screen shows nothing
            } else if (!was_on && now_on) {
                ly_          = 0;
                dot_         = 0;
                mode_        = PpuMode::OamScan;
                window_line_ = 0;
                update_stat_line();
            }
            return;
        }
        case 0xFF41:
            // Only the four source-select bits are writable; the mode and the
            // LY==LYC flag are produced by the hardware.
            stat_ = static_cast<u8>(value & 0x78);
            update_stat_line();
            return;
        case 0xFF42: scy_ = value; return;
        case 0xFF43: scx_ = value; return;
        case 0xFF44: return;                       // LY is read-only
        case 0xFF45: lyc_ = value; update_stat_line(); return;
        case 0xFF47: bgp_  = value; return;
        case 0xFF48: obp0_ = value; return;
        case 0xFF49: obp1_ = value; return;
        case 0xFF4A: wy_ = value; return;
        case 0xFF4B: wx_ = value; return;
        default: return;
    }
}

// ---------------------------------------------------------------------------
//  The STAT interrupt line
// ---------------------------------------------------------------------------
void Ppu::update_stat_line()
{
    if (!lcd_on()) { stat_line_ = false; return; }

    const bool line =
        ((stat_ & kStatLycInt)    && ly_ == lyc_)                ||
        ((stat_ & kStatHBlankInt) && mode_ == PpuMode::HBlank)   ||
        ((stat_ & kStatVBlankInt) && mode_ == PpuMode::VBlank)   ||
        ((stat_ & kStatOamInt)    && mode_ == PpuMode::OamScan);

    // Rising edge only: two sources overlapping give one interrupt, not two.
    if (line && !stat_line_) stat_irq_ = true;
    stat_line_ = line;
}

void Ppu::enter_mode(PpuMode mode)
{
    mode_ = mode;
    update_stat_line();
}

// ---------------------------------------------------------------------------
//  The sweep
// ---------------------------------------------------------------------------
void Ppu::step_dot()
{
    ++dot_;

    // Within a visible line, the mode changes at two fixed points.
    if (ly_ < kVisibleLines) {
        if (dot_ == kOamScanDots)                        enter_mode(PpuMode::Drawing);
        else if (dot_ == kOamScanDots + kDrawingDots) {
            // The line is finished: compose it now (decision D5, scanline
            // rendering) and hand the rest of the line back as HBlank.
            render_scanline();
            enter_mode(PpuMode::HBlank);
        }
    }

    if (dot_ < kDotsPerLine) return;

    // End of a line.
    dot_ = 0;
    ++ly_;

    if (ly_ == kVisibleLines) {
        // The image is complete. This is the interrupt games do all their
        // video work in, because it is the only safe window.
        enter_mode(PpuMode::VBlank);
        vblank_irq_  = true;
        frame_ready_ = true;
        ++frames_;
    } else if (ly_ >= kTotalLines) {
        ly_          = 0;
        window_line_ = 0;      // the window restarts from its own first row
        enter_mode(PpuMode::OamScan);
    } else if (ly_ < kVisibleLines) {
        enter_mode(PpuMode::OamScan);
    } else {
        update_stat_line();   // still inside VBlank, but LY changed
    }
}

// ===========================================================================
//  Rendering
// ===========================================================================
//  The console never stores an image. 160x144 pixels at 2 bits each would be
//  5.7 KiB and there are only 8 KiB of video memory in total, most of it
//  needed for other things. So it stores BUILDING BLOCKS and a PLAN, and
//  rebuilds the picture line by line, sixty times a second.
//
//    a tile      8x8 pixels, 2 bits each, so 16 bytes. Up to 384 of them.
//    a tile map  a 32x32 grid saying which tile goes in which cell.
//    a palette   the 2 bits of a pixel are an INDEX, not a colour. The
//                palette register turns an index into one of four shades,
//                which is how a game fades to black without touching a pixel.
//
//  The awkward part is the tile format: the two bits of one pixel live in TWO
//  DIFFERENT BYTES, one holding all the low bits of a row and the other all
//  the high bits.
//
//      0x3C = 0 0 1 1 1 1 0 0    <- low bits
//      0x7E = 0 1 1 1 1 1 1 0    <- high bits
//             ---------------
//      pixel  0 2 3 3 3 3 2 0    <- read vertically, column by column
// ===========================================================================

u32 Ppu::dmg_shade(u8 index)
{
    // The original greenish LCD, lightest first.
    static const u32 kShades[4] = {
        0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F,
    };
    return kShades[index & 0x03];
}

void Ppu::render_background(u8 line, std::array<u8, kScreenWidth> &bg_color)
{
    // On a DMG, clearing bit 0 of LCDC blanks the background and the window
    // entirely. Colour index 0 is left everywhere so sprites still show.
    if ((lcdc_ & LcdcBgEnable) == 0) {
        bg_color.fill(0);
        for (int x = 0; x < kScreenWidth; ++x)
            framebuffer_[static_cast<std::size_t>(line) * kScreenWidth + x] = dmg_shade(0);
        return;
    }

    const bool window_enabled = (lcdc_ & LcdcWindowEnable) != 0 && line >= wy_;
    bool       window_used    = false;

    for (int x = 0; x < kScreenWidth; ++x) {
        // The window is not a sprite: it is a second background layer that
        // does not scroll, anchored at WX-7. Games use it for score bars that
        // stay put while the scenery moves underneath.
        const bool in_window = window_enabled && (x + 7) >= static_cast<int>(wx_);

        u8  map_x, map_y;
        u16 map_base;

        if (in_window) {
            window_used = true;
            map_base = (lcdc_ & LcdcWindowTileMap) ? 0x9C00 : 0x9800;
            map_x    = static_cast<u8>(x + 7 - wx_);
            map_y    = window_line_;
        } else {
            map_base = (lcdc_ & LcdcBgTileMap) ? 0x9C00 : 0x9800;
            map_x    = static_cast<u8>(x + scx_);   // wraps at 256, as the hardware does
            map_y    = static_cast<u8>(line + scy_);
        }

        const u16 map_addr   = static_cast<u16>(map_base + (map_y / 8) * 32 + (map_x / 8));
        const u8  tile_index = vram_byte(0, map_addr);

        // Two addressing modes, and getting this wrong is a classic: with
        // LCDC bit 4 clear the index is SIGNED and counted from 0x9000.
        const u16 tile_addr = (lcdc_ & LcdcTileDataArea)
            ? static_cast<u16>(0x8000 + tile_index * 16)
            : static_cast<u16>(0x9000 + static_cast<i8>(tile_index) * 16);

        const u16 row  = static_cast<u16>(tile_addr + (map_y % 8) * 2);
        const u8  low  = vram_byte(0, row);
        const u8  high = vram_byte(0, static_cast<u16>(row + 1));
        const int bit  = 7 - (map_x % 8);

        const u8 color = static_cast<u8>((((high >> bit) & 1) << 1) | ((low >> bit) & 1));
        bg_color[static_cast<std::size_t>(x)] = color;
        framebuffer_[static_cast<std::size_t>(line) * kScreenWidth + x] =
            dmg_shade(shade_of(bgp_, color));
    }

    // Only advance the window's own line counter on lines where it appeared.
    if (window_used) ++window_line_;
}

void Ppu::render_sprites(u8 line, const std::array<u8, kScreenWidth> &bg_color)
{
    if ((lcdc_ & LcdcObjEnable) == 0) return;

    const int height = (lcdc_ & LcdcObjSize) ? 16 : 8;

    // The hardware can only handle TEN sprites per line, and it picks them by
    // their order in memory, not by where they are. Games rely on this: it is
    // why sprites flicker when too many crowd one line.
    struct Candidate { u8 y, x, tile, attr; u8 oam_index; };
    Candidate chosen[10];
    int count = 0;

    for (int i = 0; i < static_cast<int>(kSpriteCount) && count < 10; ++i) {
        const u8  sprite_y = oam_[i * 4];
        const int top      = static_cast<int>(sprite_y) - 16;   // Y is stored offset by 16
        if (line >= top && line < top + height) {
            chosen[count++] = {sprite_y, oam_[i * 4 + 1], oam_[i * 4 + 2],
                               oam_[i * 4 + 3], static_cast<u8>(i)};
        }
    }

    // Among those ten, the one further LEFT wins. Ties go to the earlier
    // entry in memory. Sorted so the winner is handled first.
    for (int i = 1; i < count; ++i) {
        Candidate key = chosen[i];
        int j = i - 1;
        while (j >= 0 && (chosen[j].x > key.x ||
                          (chosen[j].x == key.x && chosen[j].oam_index > key.oam_index))) {
            chosen[j + 1] = chosen[j];
            --j;
        }
        chosen[j + 1] = key;
    }

    // Once a sprite has claimed a pixel, a lower-priority one cannot show
    // through it, even if the winner ends up hidden behind the background.
    bool claimed[kScreenWidth] = {false};

    for (int i = 0; i < count; ++i) {
        const Candidate &s = chosen[i];

        int row = line - (static_cast<int>(s.y) - 16);
        if (s.attr & 0x40) row = height - 1 - row;         // vertical flip

        // In 8x16 mode the low bit of the index is ignored: the sprite is two
        // stacked tiles, and row 8 to 15 simply reaches into the second one.
        const u8  tile      = (height == 16) ? static_cast<u8>(s.tile & 0xFE) : s.tile;
        const u16 tile_addr = static_cast<u16>(0x8000 + tile * 16 + row * 2);
        const u8  low       = vram_byte(0, tile_addr);
        const u8  high      = vram_byte(0, static_cast<u16>(tile_addr + 1));

        for (int px = 0; px < 8; ++px) {
            const int screen_x = static_cast<int>(s.x) - 8 + px;   // X is offset by 8
            if (screen_x < 0 || screen_x >= kScreenWidth) continue;
            if (claimed[screen_x]) continue;

            const int bit   = (s.attr & 0x20) ? px : 7 - px;       // horizontal flip
            const u8  color = static_cast<u8>((((high >> bit) & 1) << 1) | ((low >> bit) & 1));
            if (color == 0) continue;   // index 0 is transparent for sprites, always

            claimed[screen_x] = true;

            // Attribute bit 7 puts the sprite BEHIND background colours 1 to
            // 3, but still in front of colour 0.
            if ((s.attr & 0x80) && bg_color[static_cast<std::size_t>(screen_x)] != 0) continue;

            const u8 palette = (s.attr & 0x10) ? obp1_ : obp0_;
            framebuffer_[static_cast<std::size_t>(line) * kScreenWidth + screen_x] =
                dmg_shade(shade_of(palette, color));
        }
    }
}

void Ppu::render_scanline()
{
    if (ly_ >= kVisibleLines) return;

    std::array<u8, kScreenWidth> bg_color{};
    render_background(ly_, bg_color);
    render_sprites(ly_, bg_color);
}

void Ppu::tick(u32 t_sys)
{
    elapsed_ += t_sys;
    if (!lcd_on()) return;          // a screen that is off sweeps nothing
    for (u32 i = 0; i < t_sys; ++i) step_dot();
}

}  // namespace retroemu
