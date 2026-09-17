#pragma once
// ===========================================================================
//  The master clock — decision D8.
// ===========================================================================
//  In the real console the CPU, the PPU, the timer and the DMA engine all run
//  at the same time off one crystal. Our program is sequential, so we simulate
//  simultaneity by keeping a shared count of elapsed T-cycles and letting each
//  component catch up to it.
//
//  Time is counted in TWO units, and the difference matters:
//
//    t_cpu   cycles at the CPU's CURRENT speed.
//            The timer (DIV/TIMA) lives in this domain.
//
//    t_sys   cycles at the CONSTANT system speed.
//            The PPU lives in this domain: the screen must still refresh
//            59.727 times per second no matter what the CPU is doing.
//
//  At normal speed the two are identical. In CGB double-speed mode (subject
//  V.6, p.8) the CPU runs twice as fast while the PPU does not, so t_sys
//  advances at half the rate of t_cpu.
//
//  Writing `ppu.tick(n)` with n counted in CPU cycles is the mistake this
//  class exists to prevent. Getting it wrong is only discovered at step 14,
//  by which point fixing it means rewriting the CPU, PPU, timer and DMA.
// ===========================================================================

#include "retroemu/core/types.hpp"

namespace retroemu {

// One machine cycle. Every memory access takes exactly this long.
inline constexpr u32 kTCyclesPerMCycle = 4;

// Hardware constants (system domain, so unaffected by double speed).
inline constexpr u32 kSystemClockHz    = 4194304;   // 4.194304 MHz
inline constexpr u32 kTCyclesPerFrame  = 70224;     // -> 59.727 frames per second

class Clock {
public:
    // Advance by `t` cycles measured in the CPU domain.
    // Returns how many SYSTEM cycles elapsed, so callers never have to redo
    // the conversion themselves: one place owns it, and it cannot drift.
    u32 advance(u32 t)
    {
        t_cpu_ += t;

        if (!double_speed_) {
            t_sys_ += t;
            return t;
        }
        // Double speed: the system domain advances half as fast. Accesses are
        // always multiples of 4 so this division is exact, but the remainder
        // is carried anyway so an odd count can never make the clocks drift.
        const u32 total   = t + sys_remainder_;
        const u32 elapsed = total / 2;
        t_sys_           += elapsed;
        sys_remainder_    = total % 2;
        return elapsed;
    }

    u64  t_cpu() const { return t_cpu_; }
    u64  t_sys() const { return t_sys_; }
    bool double_speed() const { return double_speed_; }

    void set_double_speed(bool on)
    {
        if (on == double_speed_) return;
        double_speed_  = on;
        sys_remainder_ = 0;
    }

    void reset()
    {
        t_cpu_ = 0;
        t_sys_ = 0;
        sys_remainder_ = 0;
        double_speed_ = false;
    }

private:
    u64  t_cpu_         = 0;
    u64  t_sys_         = 0;
    u32  sys_remainder_ = 0;
    bool double_speed_  = false;
};

}  // namespace retroemu
