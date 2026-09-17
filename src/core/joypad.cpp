#include "retroemu/core/joypad.hpp"

#include <cstring>

namespace retroemu {
namespace {

constexpr u8 kSelectAction    = 0x20;   // bit 5: A, B, Select, Start
constexpr u8 kSelectDirection = 0x10;   // bit 4: the control pad

}  // namespace

const char *to_string(Button button)
{
    switch (button) {
        case ButtonRight:  return "right";
        case ButtonLeft:   return "left";
        case ButtonUp:     return "up";
        case ButtonDown:   return "down";
        case ButtonA:      return "a";
        case ButtonB:      return "b";
        case ButtonSelect: return "select";
        case ButtonStart:  return "start";
    }
    return "?";
}

bool parse_button(const char *name, Button &out)
{
    static const struct { const char *name; Button button; } kNames[] = {
        {"right", ButtonRight}, {"left", ButtonLeft}, {"up", ButtonUp}, {"down", ButtonDown},
        {"a", ButtonA}, {"b", ButtonB}, {"select", ButtonSelect}, {"start", ButtonStart},
    };
    for (const auto &entry : kNames) {
        if (std::strcmp(name, entry.name) == 0) { out = entry.button; return true; }
    }
    return false;
}

void Joypad::reset()
{
    buttons_ = 0;
    // The boot sequence leaves both halves selected, which is why JOYP reads
    // 0xCF on a freshly started machine.
    select_  = 0x00;
    irq_     = false;
}

// The four wires the selected half puts on the register, inverted: 0 is down.
u8 Joypad::wires() const
{
    u8 low = 0x0F;   // nothing pressed

    if ((select_ & kSelectDirection) == 0)
        low &= static_cast<u8>(~(buttons_ & 0x0F) & 0x0F);

    // Both halves may be selected at once. A pressed button in either half
    // pulls the same wire low, so the two results are ANDed rather than one
    // replacing the other.
    if ((select_ & kSelectAction) == 0)
        low &= static_cast<u8>(~((buttons_ >> 4) & 0x0F) & 0x0F);

    return low;
}

u8 Joypad::read() const
{
    // Bits 6 and 7 are not wired and read as 1.
    return static_cast<u8>(0xC0 | (select_ & 0x30) | wires());
}

void Joypad::write(u8 value)
{
    const u8 before = wires();
    select_ = static_cast<u8>(value & 0x30);

    // Selecting a half whose buttons are already down pulls wires low, which
    // is a falling edge like any other.
    const u8 after = wires();
    if ((before & ~after) != 0) irq_ = true;
}

void Joypad::set(Button button, bool pressed)
{
    const u8 before = wires();

    if (pressed) buttons_ |= button;
    else         buttons_ = static_cast<u8>(buttons_ & ~button);

    const u8 after = wires();
    if ((before & ~after) != 0) irq_ = true;
}

}  // namespace retroemu
