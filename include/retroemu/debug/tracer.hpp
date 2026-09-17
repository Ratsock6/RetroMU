#pragma once
// ===========================================================================
//  Execution tracer and differential validation.
// ===========================================================================
//  A broken emulator says nothing. No exception, no message: a white screen,
//  or a freeze. And the mistake usually happened hundreds of thousands of
//  instructions before the symptom, which makes stepping through it by hand
//  hopeless.
//
//  The technique that does work is the differential trace. Log the complete
//  CPU state on every instruction, produce the same log from an emulator
//  known to be correct, and diff the two. The FIRST line that differs names
//  the exact instruction that went wrong.
//
//  The line format below is the one the community's reference logs use, so a
//  trace produced here can be compared against them directly:
//
//    A:01 F:B0 B:00 C:13 D:00 E:D8 H:01 L:4D SP:FFFE PC:0100 PCMEM:00,C3,50,01
//
//  PCMEM is the four bytes at PC, which makes a divergence readable without
//  having to look the address up in the ROM.
//
//  Tracing reads memory through Bus::peek, so it never advances the clock
//  (decision D15): the traced run is identical to the untraced one.
// ===========================================================================

#include <cstdio>
#include <string>

#include "retroemu/core/gameboy.hpp"

namespace retroemu {

// One line describing the machine as it stands, before the next instruction.
std::string trace_line(const GameBoy &gb);

struct TraceOptions {
    u64  max_instructions = 1000000;    // a full trace would be gigabytes
    u64  max_cycles       = 250000000ULL;
    bool ly_stub          = false;      // see Bus::set_ly_stub
};

// Run the machine, writing one line per instruction. Returns how many lines
// were produced.
u64 run_trace(GameBoy &gb, std::FILE *out, const TraceOptions &options);

// Compare two trace files and report the first divergence, with context and a
// breakdown of which register or flag differs. Returns 0 when identical.
int diff_traces(const std::string &path_a, const std::string &path_b, int context);

// A digest of the trace of the first `instructions` instructions. Used to
// detect any unintended change in CPU behaviour across later steps.
std::string trace_digest(GameBoy &gb, u64 instructions);

}  // namespace retroemu
