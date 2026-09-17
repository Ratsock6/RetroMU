#include "retroemu/core/timer.hpp"

#include "retroemu/core/bus.hpp"   // for Model

namespace retroemu {
namespace {

// TAC bits 0-1 select which bit of the internal counter TIMA watches.
// The order is not sorted: 00 is the slowest, 01 the fastest.
constexpr int kWatchedBits[4] = {9, 3, 5, 7};

constexpr u8 kTacEnable = 0x04;   // bit 2

}  // namespace

void Timer::reset(Model model)
{
    // The internal counter is left where the boot sequence leaves it, which
    // makes DIV read 0xAB on a DMG. The mandatory part skips the boot ROM
    // (ambiguity A3), so it is set directly.
    counter_      = (model == Model::Cgb) ? 0x1EA0 : 0xABCC;
    tima_         = 0x00;
    tma_          = 0x00;
    tac_          = 0xF8;   // the top five bits read as 1
    last_signal_  = edge_signal();
    reload_delay_ = 0;
}

int Timer::watched_bit() const { return kWatchedBits[tac_ & 0x03]; }

bool Timer::edge_signal() const
{
    if ((tac_ & kTacEnable) == 0) return false;
    return ((counter_ >> watched_bit()) & 1) != 0;
}

u8 Timer::read(u16 addr) const
{
    switch (addr) {
        case 0xFF04: return div();
        case 0xFF05: return tima_;
        case 0xFF06: return tma_;
        case 0xFF07: return static_cast<u8>(0xF8 | (tac_ & 0x07));
        default:     return 0xFF;
    }
}

void Timer::write(u16 addr, u8 value)
{
    switch (addr) {
        case 0xFF04:
            // Any write resets the whole internal counter, not just DIV.
            // If the watched bit was set, that reset is a falling edge and
            // TIMA increments as a side effect.
            counter_ = 0;
            break;

        case 0xFF05:
            // Writing during the one-cycle reload window cancels both the
            // reload and the pending interrupt.
            tima_         = value;
            reload_delay_ = 0;
            return;

        case 0xFF06:
            tma_ = value;
            // A write landing inside the reload window takes effect
            // immediately: the value copied into TIMA is the new one.
            if (reload_delay_ > 0) tima_ = value;
            return;

        case 0xFF07:
            // Changing the selected bit, or disabling the timer, can also
            // produce a falling edge of the watched signal.
            tac_ = static_cast<u8>(value & 0x07);
            break;

        default:
            return;
    }

    // Both DIV and TAC writes can change the signal, so the edge detector is
    // run here exactly as it is on every cycle.
    const bool signal = edge_signal();
    if (last_signal_ && !signal) {
        if (++tima_ == 0) reload_delay_ = 4;   // overflow: reload one cycle later
    }
    last_signal_ = signal;
}

bool Timer::tick_one()
{
    bool irq = false;

    // The pending reload from a previous overflow happens before anything
    // else this cycle.
    if (reload_delay_ > 0 && --reload_delay_ == 0) {
        tima_ = tma_;
        irq   = true;
    }

    ++counter_;

    const bool signal = edge_signal();
    if (last_signal_ && !signal) {
        if (++tima_ == 0) reload_delay_ = 4;
    }
    last_signal_ = signal;

    return irq;
}

bool Timer::tick(u32 t)
{
    bool irq = false;
    for (u32 i = 0; i < t; ++i) {
        if (tick_one()) irq = true;
    }
    return irq;
}

}  // namespace retroemu
