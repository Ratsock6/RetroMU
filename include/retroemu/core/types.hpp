#pragma once
// ===========================================================================
//  Core emulator types.
// ===========================================================================
//  The emulated hardware is 8/16-bit. Using plain `int` everywhere would hide
//  wrap-around (255 + 1 must yield 0, not 256) and silently make the
//  emulation wrong. These aliases make the width explicit at every
//  declaration.
// ===========================================================================

#include <cstddef>
#include <cstdint>

namespace retroemu {

using u8  = std::uint8_t;    // an 8-bit register, one memory cell
using u16 = std::uint16_t;   // an address, a register pair
using u32 = std::uint32_t;   // a cycle count, an ARGB colour
using u64 = std::uint64_t;   // a cumulative cycle count

using i8  = std::int8_t;     // a signed relative offset (JR instruction)
using i16 = std::int16_t;
using i32 = std::int32_t;

// --- Screen geometry (identical on DMG and CGB) ---------------------------
inline constexpr int kScreenWidth  = 160;
inline constexpr int kScreenHeight = 144;
inline constexpr std::size_t kScreenPixels =
    static_cast<std::size_t>(kScreenWidth) * kScreenHeight;

}  // namespace retroemu
