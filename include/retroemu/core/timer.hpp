#pragma once
// ===========================================================================
//  Timer — DIV, TIMA, TMA and TAC.
// ===========================================================================
//  Four registers that look trivial and are not. This is the component the
//  test bundle checks most precisely (mooneye's div_timing and intr_timing),
//  and the reason decision D8 exists.
//
//  The key to everything below: DIV is NOT a counter of its own. There is one
//  internal 16-bit counter that increments on every single clock cycle, and
//  DIV (0xFF04) is simply its upper byte. Three consequences follow, and all
//  three are what the tests look for:
//
//    1. Writing to DIV does not set it to the written value. It resets the
//       WHOLE 16-bit counter to zero.
//
//    2. TIMA does not increment on a schedule. It increments on the FALLING
//       EDGE of one selected bit of that internal counter. Which bit is
//       chosen by TAC.
//
//    3. Because of 2, resetting DIV can make TIMA increment: if the watched
//       bit was 1 and the reset makes it 0, that is a falling edge. Changing
//       TAC can do the same. Neither looks like it should touch TIMA.
//
//  There is one more oddity. When TIMA overflows past 0xFF it does not reload
//  immediately: for one machine cycle it reads 0x00 and no interrupt has been
//  raised yet. Only then is TMA copied in and the interrupt requested. During
//  that window a write to TIMA cancels both.
// ===========================================================================

#include "retroemu/core/types.hpp"

namespace retroemu {

enum class Model;   // defined in bus.hpp

class Timer {
public:
    void reset(Model model);

    // 0xFF04..0xFF07
    u8   read(u16 addr) const;
    void write(u16 addr, u8 value);

    // Advance by `t` cycles measured in the CPU domain: the timer follows the
    // CPU clock, so in CGB double-speed mode it does run twice as fast.
    // Returns true when the timer interrupt must be requested.
    bool tick(u32 t);

    // The internal counter, exposed for the debugger and the tests.
    u16 internal_counter() const { return counter_; }
    u8  div() const { return static_cast<u8>(counter_ >> 8); }

private:
    // The bit of the internal counter that TIMA watches, chosen by TAC.
    int watched_bit() const;

    // The signal whose falling edge drives TIMA: the watched bit ANDed with
    // the enable bit of TAC.
    bool edge_signal() const;

    // Advance exactly one cycle. Returns true if the timer interrupt fires.
    bool tick_one();

    u16  counter_        = 0;       // increments every cycle; DIV is its high byte
    u8   tima_           = 0;       // 0xFF05
    u8   tma_            = 0;       // 0xFF06
    u8   tac_            = 0;       // 0xFF07
    bool last_signal_    = false;   // previous state, for the falling-edge detector
    u8   reload_delay_   = 0;       // cycles left before TMA is copied into TIMA
};

}  // namespace retroemu
