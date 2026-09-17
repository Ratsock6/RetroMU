#pragma once
// ===========================================================================
//  Joypad — section V.4 of the subject (p.7).
// ===========================================================================
//  "You must correctly implement the input system of the handheld console:
//   control pad (4 directions) and 4 action buttons (A, B, Start, Select)."
//
//  Eight buttons, but only ONE register to read them through, and it exposes
//  four wires at a time. The game picks which half it wants to see by
//  clearing one of two selection bits, reads the four wires, then picks the
//  other half. Reading both halves takes two writes and two reads.
//
//  Two things catch people out:
//
//    - The logic is INVERTED. A bit reads 0 when the button is DOWN. A
//      register full of ones means nothing is pressed.
//
//    - Selecting both halves at once is allowed, and the four wires then
//      carry both halves ANDed together, because a pressed button in either
//      half pulls the same wire low.
// ===========================================================================

#include "retroemu/core/types.hpp"

namespace retroemu {

// Internally a pressed button is a 1, which reads more naturally than the
// hardware's inverted wires. The inversion happens once, on read.
enum Button : u8 {
    ButtonRight  = 0x01,
    ButtonLeft   = 0x02,
    ButtonUp     = 0x04,
    ButtonDown   = 0x08,
    ButtonA      = 0x10,
    ButtonB      = 0x20,
    ButtonSelect = 0x40,
    ButtonStart  = 0x80,
};

const char *to_string(Button button);

// Parse a name such as "a", "start" or "left". Returns false if unknown.
bool parse_button(const char *name, Button &out);

class Joypad {
public:
    void reset();

    void set(Button button, bool pressed);
    bool pressed(Button button) const { return (buttons_ & button) != 0; }
    u8   state() const { return buttons_; }

    // 0xFF00
    u8   read() const;
    void write(u8 value);

    // The interrupt fires when a wire goes from high to low, which is to say
    // when a button of the SELECTED half is pressed. Drained by the bus.
    bool take_irq() { const bool f = irq_; irq_ = false; return f; }

private:
    // The four wires as the hardware presents them: 0 means pressed.
    u8 wires() const;

    u8   buttons_ = 0;      // 1 = pressed
    u8   select_  = 0x30;   // bits 4 and 5; 0 selects a half
    bool irq_     = false;
};

}  // namespace retroemu
