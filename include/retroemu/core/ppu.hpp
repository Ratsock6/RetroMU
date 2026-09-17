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

// LCDC (0xFF40) bit assignments.
enum LcdcBit : u8 {
    LcdcBgEnable      = 0x01,   // DMG: background and window are drawn at all
    LcdcObjEnable     = 0x02,
    LcdcObjSize       = 0x04,   // 0 = 8x8, 1 = 8x16
    LcdcBgTileMap     = 0x08,   // 0 = 0x9800, 1 = 0x9C00
    LcdcTileDataArea  = 0x10,   // 1 = 0x8000 unsigned, 0 = 0x8800 signed
    LcdcWindowEnable  = 0x20,
    LcdcWindowTileMap = 0x40,   // 0 = 0x9800, 1 = 0x9C00
    LcdcEnable        = 0x80,   // the screen itself
};

// CGB background/window tile attributes. They live at the SAME address as the
// tile index, but in VRAM BANK 1 (subject V.6, p.8). On a DMG that bank does
// not exist and every attribute reads as zero, which is why the two renderers
// can share one code path.
enum BgAttrBit : u8 {
    BgAttrPalette  = 0x07,   // which of the eight background palettes
    BgAttrBank     = 0x08,   // tile data comes from VRAM bank 1
    BgAttrXFlip    = 0x20,
    BgAttrYFlip    = 0x40,
    BgAttrPriority = 0x80,   // this tile wins over sprites (BG-to-OAM priority)
};

// CGB sprite attribute bits that the DMG does not have. Bits 4-7 keep their
// DMG meaning (DMG palette, X flip, Y flip, OBJ-to-BG priority).
enum ObjAttrBit : u8 {
    ObjAttrCgbPalette = 0x07,
    ObjAttrBank       = 0x08,
};

// Colour palette RAM: eight palettes of four colours, two bytes each.
inline constexpr std::size_t kCgbPaletteBytes = 64;

enum class PpuMode : u8 {
    HBlank  = 0,
    VBlank  = 1,
    OamScan = 2,
    Drawing = 3,
};

const char *to_string(PpuMode mode);

class Ppu {
public:
    // `dmg_compatibility` is set when a CGB is running a cartridge that knows
    // nothing about colour: the extra hardware is present, but the renderer
    // behaves like a DMG. See docs/decisions.md (D58).
    void reset(Model model, bool dmg_compatibility = false);

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

    // True once per visible line, the moment HBlank begins. The CGB's HBlank
    // DMA moves its next 16 bytes exactly there (step 14).
    bool take_hblank_entered() { const bool f = hblank_entered_; hblank_entered_ = false; return f; }

    // --- The produced image -------------------------------------------------
    //  160x144 pixels in ARGB8888, rebuilt one scanline at a time at the end
    //  of mode 3. This is what the frontend puts on screen.
    const std::array<u32, kScreenPixels> &framebuffer() const { return framebuffer_; }

    // The four shades a DMG displays, lightest first. A display choice rather
    // than hardware: the console has a greenish LCD, and the palette registers
    // only ever select one of these four.
    static u32 dmg_shade(u8 index);

    // A CGB colour is 15 bits, five per channel, stored little-endian as
    // -bbbbbgg gggrrrrr. The expansion to 8 bits per channel is the one the
    // cgb-acid2 author documents, so a capture can be compared byte for byte
    // with the reference image.
    static u32 cgb_color(u16 bgr555);

    // --- Inspection --------------------------------------------------------
    u8      ly() const     { return ly_; }
    PpuMode mode() const   { return mode_; }
    bool    lcd_on() const { return (lcdc_ & LcdcEnable) != 0; }
    u32     dot() const    { return dot_; }
    bool    cgb() const    { return cgb_; }
    u64     elapsed() const { return elapsed_; }
    u64     frames() const  { return frames_; }

    const std::array<u8, kVramBankSize * kVramBanks> &vram() const { return vram_; }
    const std::array<u8, kOamSize>                   &oam()  const { return oam_; }
    const std::array<u8, kCgbPaletteBytes> &bg_palette()  const { return bg_palette_; }
    const std::array<u8, kCgbPaletteBytes> &obj_palette() const { return obj_palette_; }

private:
    void step_dot();
    void enter_mode(PpuMode mode);

    // --- Rendering, run once per visible line at the end of mode 3 ---------
    void render_scanline();
    void render_background(u8 line, std::array<u8, kScreenWidth> &bg_color,
                           std::array<u8, kScreenWidth> &bg_attr);
    void render_sprites(u8 line, const std::array<u8, kScreenWidth> &bg_color,
                        const std::array<u8, kScreenWidth> &bg_attr);

    // Look a colour index up in one of the eight CGB palettes.
    u32 cgb_palette_color(const std::array<u8, kCgbPaletteBytes> &palette,
                          u8 index, u8 color) const;

    // Raw VRAM access by bank, for the renderer. Unlike read_vram it ignores
    // the VBK register: the renderer decides which bank it wants.
    u8 vram_byte(std::size_t bank, u16 addr) const
    {
        return vram_[bank * kVramBankSize + static_cast<std::size_t>(addr - 0x8000)];
    }

    // A palette register packs four 2-bit shades. Colour index 0 sits in bits
    // 0-1, index 3 in bits 6-7.
    static u8 shade_of(u8 palette, u8 color) { return static_cast<u8>((palette >> (color * 2)) & 0x03); }

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

    std::array<u32, kScreenPixels> framebuffer_{};

    // The window has a line counter of its own, which only advances on lines
    // where the window was actually drawn. That is why a window appearing
    // halfway down the screen starts from its own first row, not from the
    // screen's.
    u8 window_line_ = 0;

    // --- CGB additions (step 14) -------------------------------------------
    //  BCPS/OCPS (0xFF68/0xFF6A) hold an index into palette RAM plus an
    //  auto-increment bit, and BCPD/OCPD (0xFF69/0xFF6B) are the window onto
    //  the byte it points at. Games write 64 bytes through a 1-byte hole.
    std::array<u8, kCgbPaletteBytes> bg_palette_{};
    std::array<u8, kCgbPaletteBytes> obj_palette_{};
    u8 bcps_ = 0;      // 0xFF68
    u8 ocps_ = 0;      // 0xFF6A
    u8 opri_ = 0;      // 0xFF6C  0 = sprite priority by OAM index (CGB rule)

    // True when the colour renderer is in use: a CGB running a cartridge that
    // asks for colour. A CGB running a black-and-white cartridge leaves this
    // false and renders exactly like a DMG.
    bool cgb_ = false;

    bool hblank_entered_ = false;

    u8  vram_bank_ = 0;
    u64 elapsed_   = 0;
    u64 frames_    = 0;
};

}  // namespace retroemu
