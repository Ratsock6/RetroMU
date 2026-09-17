#pragma once
// ===========================================================================
//  PPU — Picture Processing Unit.
// ===========================================================================
//  STEP 3 SCOPE: the PPU exists only to OWN the memory the bus has to route
//  to it, namely VRAM and OAM, and to accept cycles from the clock. The state
//  machine arrives in step 8 and the actual rendering in step 9.
//
//  Creating the class now rather than parking VRAM inside the Bus means the
//  later steps only add behaviour, never move data around.
// ===========================================================================

#include <array>

#include "retroemu/core/types.hpp"

namespace retroemu {

// VRAM is 8 KiB on DMG. The CGB adds a second bank selected by VBK (0xFF4F),
// which is where tile attributes live (subject V.6, p.8).
inline constexpr std::size_t kVramBankSize = 8 * 1024;
inline constexpr std::size_t kVramBanks    = 2;

// Object Attribute Memory: 40 sprites of 4 bytes each.
inline constexpr std::size_t kOamSize    = 160;
inline constexpr std::size_t kSpriteCount = 40;

class Ppu {
public:
    void reset();

    // --- Memory owned by the PPU -------------------------------------------
    // Addresses are passed in CPU space (0x8000-0x9FFF, 0xFE00-0xFE9F); the
    // PPU maps them onto its own storage and applies the VRAM bank.
    u8   read_vram(u16 addr) const;
    void write_vram(u16 addr, u8 value);
    u8   read_oam(u16 addr) const;
    void write_oam(u16 addr, u8 value);

    // --- VRAM bank select, CGB only (VBK, 0xFF4F) --------------------------
    u8   vram_bank_register() const;
    void set_vram_bank_register(u8 value);

    // --- Clock -------------------------------------------------------------
    // Cycles are given in the SYSTEM domain: the PPU never speeds up, even in
    // CGB double-speed mode.
    void tick(u32 t_sys);
    u64  elapsed() const { return elapsed_; }

    // Direct access for the debugger and, later, the renderer.
    const std::array<u8, kVramBankSize * kVramBanks> &vram() const { return vram_; }
    const std::array<u8, kOamSize>                   &oam()  const { return oam_; }

private:
    std::array<u8, kVramBankSize * kVramBanks> vram_{};
    std::array<u8, kOamSize>                   oam_{};
    u8  vram_bank_ = 0;
    u64 elapsed_   = 0;
};

}  // namespace retroemu
