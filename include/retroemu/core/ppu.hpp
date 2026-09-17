#pragma once
// ===========================================================================
//  PPU — Picture Processing Unit.
// ===========================================================================
//  STEP 8 SCOPE: the state machine, not the pixels. The PPU now sweeps the
//  screen the way the hardware does and raises the interrupts that go with
//  it. Drawing arrives in step 9.
//
//  The screen is not refreshed all at once. An electron beam sweeps it line
//  by line, and the PPU cycles through four modes on every one of them:
//
//      mode 2  OAM scan    80 dots    which sprites land on this line
//      mode 3  drawing    172 dots    the line is produced
//      mode 0  HBlank     204 dots    idle until the next line
//                        ---------
//                         456 dots per line
//
//      144 visible lines, then 10 more of mode 1 (VBlank) = 154 lines
//      154 x 456 = 70224 dots per frame = 59.727 frames per second
//
//  Two things make this worth getting right rather than just drawing a whole
//  image at once:
//
//    - LY (0xFF44) tells a game which line is being swept. Games poll it and
//      wait. dmg-acid2 spins on it forever until this exists, which the
//      step 6 tracer showed at instruction 12.
//
//    - VBlank is the only period where writing to video memory is safe, so
//      games do all their drawing work in the interrupt it raises.
// ===========================================================================

#include <array>

#include "retroemu/core/types.hpp"

namespace retroemu {

enum class Model;   // defined in bus.hpp

// VRAM is 8 KiB on DMG. The CGB adds a second bank selected by VBK (0xFF4F),
// which is where tile attributes live (subject V.6, p.8).
inline constexpr std::size_t kVramBankSize = 8 * 1024;
inline constexpr std::size_t kVramBanks    = 2;

// Object Attribute Memory: 40 sprites of 4 bytes each.
inline constexpr std::size_t kOamSize     = 160;
inline constexpr std::size_t kSpriteCount = 40;

// Screen sweep geometry.
inline constexpr u32 kDotsPerLine    = 456;
inline constexpr u32 kOamScanDots    = 80;
inline constexpr u32 kDrawingDots    = 172;   // shortest case; step 9 may vary it
inline constexpr u8  kVisibleLines   = 144;
inline constexpr u8  kTotalLines     = 154;

enum class PpuMode : u8 {
    HBlank  = 0,
    VBlank  = 1,
    OamScan = 2,
    Drawing = 3,
};

const char *to_string(PpuMode mode);

class Ppu {
public:
    void reset(Model model);

    // --- Memory owned by the PPU -------------------------------------------
    u8   read_vram(u16 addr) const;
    void write_vram(u16 addr, u8 value);
    u8   read_oam(u16 addr) const;
    void write_oam(u16 addr, u8 value);

    // --- Registers 0xFF40-0xFF4B (0xFF46, the DMA trigger, is step 10) -----
    u8   read(u16 addr) const;
    void write(u16 addr, u8 value);

    // --- VRAM bank select, CGB only (VBK, 0xFF4F) --------------------------
    u8   vram_bank_register() const;
    void set_vram_bank_register(u8 value);

    // --- Clock -------------------------------------------------------------
    // Cycles are given in the SYSTEM domain: the PPU never speeds up, even in
    // CGB double-speed mode.
    void tick(u32 t_sys);

    // --- Signals drained by the bus ----------------------------------------
    bool take_vblank_irq() { const bool f = vblank_irq_; vblank_irq_ = false; return f; }
    bool take_stat_irq()   { const bool f = stat_irq_;   stat_irq_   = false; return f; }

    // True once per completed frame. The frontend uses it to know when the
    // image is ready to present.
    bool take_frame_ready() { const bool f = frame_ready_; frame_ready_ = false; return f; }

    // --- Inspection --------------------------------------------------------
    u8      ly() const     { return ly_; }
    PpuMode mode() const   { return mode_; }
    bool    lcd_on() const { return (lcdc_ & 0x80) != 0; }
    u32     dot() const    { return dot_; }
    u64     elapsed() const { return elapsed_; }
    u64     frames() const  { return frames_; }

    const std::array<u8, kVramBankSize * kVramBanks> &vram() const { return vram_; }
    const std::array<u8, kOamSize>                   &oam()  const { return oam_; }

private:
    void step_dot();
    void enter_mode(PpuMode mode);

    // The STAT interrupt does not fire once per selected event. All the
    // enabled sources are OR-ed into one internal line, and only a rising
    // edge of that line raises the interrupt. Two events overlapping
    // therefore produce ONE interrupt, not two.
    void update_stat_line();

    std::array<u8, kVramBankSize * kVramBanks> vram_{};
    std::array<u8, kOamSize>                   oam_{};

    u8 lcdc_ = 0x91;   // 0xFF40  bit 7 turns the screen on
    u8 stat_ = 0x85;   // 0xFF41  bits 3-6 select STAT interrupt sources
    u8 scy_  = 0;      // 0xFF42
    u8 scx_  = 0;      // 0xFF43
    u8 ly_   = 0;      // 0xFF44  read-only: the line being swept
    u8 lyc_  = 0;      // 0xFF45  compare value
    u8 bgp_  = 0xFC;   // 0xFF47
    u8 obp0_ = 0xFF;   // 0xFF48
    u8 obp1_ = 0xFF;   // 0xFF49
    u8 wy_   = 0;      // 0xFF4A
    u8 wx_   = 0;      // 0xFF4B

    PpuMode mode_ = PpuMode::OamScan;
    u32     dot_  = 0;          // position within the current line

    bool stat_line_   = false;  // previous state, for the rising-edge detector
    bool vblank_irq_  = false;
    bool stat_irq_    = false;
    bool frame_ready_ = false;

    u8  vram_bank_ = 0;
    u64 elapsed_   = 0;
    u64 frames_    = 0;
};

}  // namespace retroemu
