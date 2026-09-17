#pragma once
// ===========================================================================
//  CPU registers.
// ===========================================================================
//  Seven 8-bit registers plus two 16-bit ones. The 8-bit registers pair up,
//  and an instruction may address either view of the same storage:
//
//      A  F   ->  AF        A is the accumulator, F holds the flags
//      B  C   ->  BC
//      D  E   ->  DE
//      H  L   ->  HL        the usual pointer into memory
//
//  F is special: only its top four bits exist as real wires. The low nibble
//  is permanently zero, and writing to it changes nothing. Forgetting that is
//  a classic source of failures in blargg's cpu_instrs.
// ===========================================================================

#include "retroemu/core/types.hpp"

namespace retroemu {

// The four flag bits inside F.
enum Flag : u8 {
    FlagZ = 0x80,   // result was zero
    FlagN = 0x40,   // the operation was a subtraction
    FlagH = 0x20,   // carry out of bit 3 into bit 4 (half-carry)
    FlagC = 0x10,   // carry out of bit 7
};

struct Registers {
    u8  a = 0, f = 0;
    u8  b = 0, c = 0;
    u8  d = 0, e = 0;
    u8  h = 0, l = 0;
    u16 sp = 0;
    u16 pc = 0;

    u16 af() const { return static_cast<u16>((a << 8) | f); }
    u16 bc() const { return static_cast<u16>((b << 8) | c); }
    u16 de() const { return static_cast<u16>((d << 8) | e); }
    u16 hl() const { return static_cast<u16>((h << 8) | l); }

    // The low nibble of F is discarded: those bits do not exist in hardware.
    void set_af(u16 v) { a = static_cast<u8>(v >> 8); f = static_cast<u8>(v & 0xF0); }
    void set_bc(u16 v) { b = static_cast<u8>(v >> 8); c = static_cast<u8>(v & 0xFF); }
    void set_de(u16 v) { d = static_cast<u8>(v >> 8); e = static_cast<u8>(v & 0xFF); }
    void set_hl(u16 v) { h = static_cast<u8>(v >> 8); l = static_cast<u8>(v & 0xFF); }

    bool flag(u8 mask) const { return (f & mask) != 0; }

    void set_flag(u8 mask, bool on)
    {
        if (on) f |= mask;
        else    f = static_cast<u8>(f & ~mask);
    }

    void set_flags(bool z, bool n, bool half, bool carry)
    {
        f = static_cast<u8>((z ? FlagZ : 0) | (n ? FlagN : 0) |
                            (half ? FlagH : 0) | (carry ? FlagC : 0));
    }
};

}  // namespace retroemu
