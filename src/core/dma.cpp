#include "retroemu/core/dma.hpp"

#include "retroemu/core/bus.hpp"

namespace retroemu {
namespace {

constexpr u32 kCyclesPerByte = 4;   // one machine cycle

}  // namespace

void Dma::reset()
{
    page_        = 0xFF;
    index_       = 0;
    accumulator_ = 0;
    delay_       = 0;
    active_      = false;
}

void Dma::start(u8 page)
{
    page_  = page;
    index_ = 0;
    // The write itself occupies a machine cycle before the first byte moves.
    // A transfer requested while one is already running simply restarts it.
    delay_       = 1;
    accumulator_ = 0;
    active_      = true;
}

void Dma::tick(Bus &bus, u32 t)
{
    if (!active_) return;

    accumulator_ += t;
    while (accumulator_ >= kCyclesPerByte && active_) {
        accumulator_ -= kCyclesPerByte;

        if (delay_ > 0) { --delay_; continue; }

        // peek and poke, never read and write: the copier must not charge the
        // clock a second time for cycles this tick has already accounted for.
        const u16 source = static_cast<u16>((page_ << 8) | index_);
        bus.poke(static_cast<u16>(0xFE00 + index_), bus.peek(source));

        if (++index_ >= kOamDmaLength) active_ = false;
    }
}

}  // namespace retroemu
