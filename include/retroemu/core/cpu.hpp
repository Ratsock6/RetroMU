#pragma once
// ===========================================================================
//  CPU — the Sharp SM83, a close relative of the Z80 (subject Ch. I, p.3).
// ===========================================================================
//  Roughly 500 instructions: 256 one-byte opcodes plus 256 more behind the
//  0xCB prefix. They are NOT decoded with a 500-case switch. The opcode map
//  is highly regular, so the bit fields are extracted instead:
//
//      x = op >> 6      y = (op >> 3) & 7      z = op & 7
//      p = y >> 1       q = y & 1
//
//  All of 0x40-0x7F, for instance, is "LD destination, source" where y picks
//  the destination and z the source. Forty lines replace sixty-four cases.
//
//  NOTE ON TIMING (decision D8): this CPU does NOT count its own cycles.
//  Every memory access goes through the Bus, which charges 4 T-cycles at the
//  moment it happens. Only the internal cycles that correspond to no memory
//  access are added explicitly, with bus.tick().
// ===========================================================================

#include "retroemu/core/bus.hpp"
#include "retroemu/core/registers.hpp"
#include "retroemu/core/types.hpp"

namespace retroemu {

class Cpu {
public:
    // Post-boot state. The mandatory part skips the boot ROM (ambiguity A3 in
    // docs/decisions.md), so the registers are set to the values the real boot
    // sequence leaves behind. The values differ between DMG and CGB.
    void reset(Model model);

    // Execute one instruction, or service one interrupt, or burn one cycle
    // while halted. Returns how many T-cycles elapsed, measured on the bus.
    u32 step(Bus &bus);

    Registers       &regs()       { return regs_; }
    const Registers &regs() const { return regs_; }

    bool ime() const     { return ime_; }
    bool halted() const  { return halted_; }
    bool stopped() const { return stopped_; }

    // Set when an opcode that does not exist on the real chip is executed.
    // Hardware locks up in that case; we stop and report instead.
    bool illegal() const { return illegal_; }
    u8   illegal_opcode() const { return illegal_opcode_; }

private:
    // --- Fetching ----------------------------------------------------------
    u8  fetch8(Bus &bus);
    u16 fetch16(Bus &bus);

    // --- Decoding ----------------------------------------------------------
    void execute(Bus &bus, u8 opcode);
    void execute_cb(Bus &bus, u8 opcode);

    // --- Operand helpers ---------------------------------------------------
    // Index order used throughout the opcode map: B C D E H L (HL) A.
    // Index 6 is a memory access through HL, so it costs 4 extra cycles.
    u8   read_r8(Bus &bus, int index);
    void write_r8(Bus &bus, int index, u8 value);
    u16  read_rp(int index) const;            // BC DE HL SP
    void write_rp(int index, u16 value);
    u16  read_rp2(int index) const;           // BC DE HL AF
    void write_rp2(int index, u16 value);
    bool condition(int index) const;          // NZ Z NC C

    // --- Stack -------------------------------------------------------------
    void push16(Bus &bus, u16 value);
    u16  pop16(Bus &bus);

    // --- Arithmetic --------------------------------------------------------
    void alu(int operation, u8 value);
    u8   inc8(u8 value);
    u8   dec8(u8 value);
    void add_hl(Bus &bus, u16 value);
    u16  add_sp_signed(Bus &bus, i8 offset);
    void daa();
    u8   rotate(int operation, u8 value, bool cb_form);

    // --- Interrupts --------------------------------------------------------
    // Returns true when an interrupt was serviced this step.
    bool service_interrupt(Bus &bus);

    Registers regs_;
    bool ime_         = false;   // interrupt master enable
    bool ime_pending_ = false;   // EI takes effect one instruction later
    bool halted_      = false;
    bool halt_bug_    = false;   // documented hardware quirk, see cpu.cpp
    bool stopped_     = false;
    bool illegal_     = false;
    u8   illegal_opcode_ = 0;
};

}  // namespace retroemu
