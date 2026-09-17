#pragma once
// ===========================================================================
//  Built-in CPU self-test.
// ===========================================================================
//  blargg's test ROMs are the real validation, but they are not committed:
//  only the MIT bundle belongs in this repository (subject Ch. VII, p.11).
//  This suite therefore ships inside the executable, so the instruction set
//  can always be checked with no external file at all.
//
//  Each case runs one instruction on a synthetic cartridge and compares the
//  resulting registers and flags against values taken from the hardware
//  documentation. The cases concentrate on what actually breaks: the
//  half-carry, the flags that are left untouched, and the instructions whose
//  behaviour is counter-intuitive.
// ===========================================================================

namespace retroemu {

// Returns the number of failures; 0 means everything passed.
int run_cpu_selftest(bool verbose);

}  // namespace retroemu
