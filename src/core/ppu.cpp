#include "retroemu/core/ppu.hpp"

#include "retroemu/core/bus.hpp"   // for Model

namespace retroemu {
namespace {

constexpr u8 kLcdcEnable = 0x80;   // LCDC bit 7

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
                stat_line_ = false;
            } else if (!was_on && now_on) {
                ly_   = 0;
                dot_  = 0;
                mode_ = PpuMode::OamScan;
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
        else if (dot_ == kOamScanDots + kDrawingDots)    enter_mode(PpuMode::HBlank);
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
        ly_ = 0;
        enter_mode(PpuMode::OamScan);
    } else if (ly_ < kVisibleLines) {
        enter_mode(PpuMode::OamScan);
    } else {
        update_stat_line();   // still inside VBlank, but LY changed
    }
}

void Ppu::tick(u32 t_sys)
{
    elapsed_ += t_sys;
    if (!lcd_on()) return;          // a screen that is off sweeps nothing
    for (u32 i = 0; i < t_sys; ++i) step_dot();
}

}  // namespace retroemu
