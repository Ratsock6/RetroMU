#pragma once
// ===========================================================================
//  Disassembler — required by section V.1 of the subject (p.7).
// ===========================================================================
//  "You must also have a debugger allowing at least to [...] display the next
//   instruction to execute."
//
//  That one line forces a whole component into existence: the mirror of the
//  CPU's decoder. Where the decoder reads 0x80 and PERFORMS A = A + B, this
//  reads 0x80 and PRODUCES the text "ADD A, B".
//
//  Two properties matter:
//
//    - It is decoded by bit field, exactly like the CPU (decision D17), so
//      the two stay structurally identical and cannot drift apart.
//
//    - It has NO side effect. It reads memory through Bus::peek, which does
//      not advance the clock (decision D15). Looking at the machine must
//      never perturb the machine it is looking at.
// ===========================================================================

#include <array>
#include <string>

#include "retroemu/core/bus.hpp"
#include "retroemu/core/types.hpp"

namespace retroemu {

struct Instruction {
    u16              address = 0;   // where it starts
    u8               length  = 1;   // 1 to 3 bytes
    std::array<u8, 3> bytes{};      // the raw encoding, for display
    std::string      text;          // "LD B, D", "JP $C350", ...

    // "C3 50 01" — the bytes as they appear in memory.
    std::string hex() const;
};

// Decode the instruction at `address`. Never writes, never ticks.
Instruction disassemble(const Bus &bus, u16 address);

}  // namespace retroemu
