#pragma once
// ===========================================================================
//  OAM DMA — the sprite table copier.
// ===========================================================================
//  A game has to rewrite the whole 160-byte sprite table on most frames, and
//  the CPU is far too slow for that: 160 byte-by-byte copies would eat a
//  large slice of the VBlank window, which is the only safe moment to do it.
//
//  So the console has a dedicated copier. Writing one byte to 0xFF46 says
//  "copy 160 bytes from page XX00 into the sprite table", and it happens on
//  its own while the CPU carries on with something else. One byte per machine
//  cycle, 160 cycles in all.
//
//  This is the clearest example in the whole machine of what decision D8 was
//  about: an address that stores nothing and STARTS something instead.
// ===========================================================================

#include "retroemu/core/types.hpp"

namespace retroemu {

class Bus;   // the copier reads through the bus, so only the name is needed here

inline constexpr u16 kOamDmaRegister = 0xFF46;
inline constexpr u16 kOamDmaLength   = 160;   // one byte per machine cycle

class Dma {
public:
    void reset();

    // Writing to 0xFF46 starts, or restarts, a transfer.
    void start(u8 page);

    // Reading 0xFF46 gives back the page that was written.
    u8 source_page() const { return page_; }

    bool active() const { return active_; }
    u16  progress() const { return index_; }

    // Advance by `t` cycles of the CPU domain: the copier moves one byte per
    // machine cycle, so in CGB double-speed mode it finishes in half the real
    // time, exactly as the hardware does.
    void tick(Bus &bus, u32 t);

private:
    u8   page_        = 0xFF;
    u16  index_       = 0;      // how many bytes have been copied
    u32  accumulator_ = 0;      // leftover cycles below one machine cycle
    u8   delay_       = 0;      // machine cycles before the first byte moves
    bool active_      = false;
};

}  // namespace retroemu
