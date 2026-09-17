#pragma once
// ===========================================================================
//  Bus / MMU — the cornerstone of the emulator (decisions D3 and D8).
// ===========================================================================
//  The CPU sees ONE thing: 65536 addresses. It says "give me the byte at X"
//  and something answers. That something is this class.
//
//  Two properties make it the most important file in the project.
//
//  1. IT IS NOT AN ARRAY.
//     Some addresses do not store anything, they DO something:
//       write 0xFF46 -> starts a 160-byte DMA transfer into OAM
//       write 0xFF40 -> turns the screen off
//       write 0x2000 -> that is ROM, read-only; nothing is stored, but the
//                       cartridge switches to another bank
//     A plain `u8 memory[65536]` can never express this.
//
//  2. EVERY ACCESS ADVANCES THE CLOCK BY 4 CYCLES, AS IT HAPPENS.
//     Not at the end of the instruction: right here. That is what lets the
//     timer and the PPU observe the exact moment of each access, which is
//     what mooneye's div_timing, intr_timing and oam_dma tests check.
//     The CPU therefore never has to count its own cycles.
//
//  `peek` exists for the debugger and the disassembler: same dispatch, but no
//  clock advance and no side effect, so inspecting memory never perturbs the
//  emulation.
// ===========================================================================

#include <array>
#include <string>

#include "retroemu/core/cartridge.hpp"
#include "retroemu/core/clock.hpp"
#include "retroemu/core/ppu.hpp"
#include "retroemu/core/types.hpp"

namespace retroemu {

// Which machine we are emulating. Taken from the cartridge header by default,
// overridable later for the "forcing DMG/CGB" bonus (subject Ch. VI, p.9).
enum class Model { Dmg, Cgb };

// --- The regions of the 64 KiB address space -------------------------------
enum class MemRegion {
    RomBank0,          // 0x0000-0x3FFF  fixed cartridge bank
    RomBankN,          // 0x4000-0x7FFF  switchable cartridge bank
    Vram,              // 0x8000-0x9FFF  owned by the PPU
    ExternalRam,       // 0xA000-0xBFFF  inside the cartridge, may be battery-backed
    WramBank0,         // 0xC000-0xCFFF
    WramBankN,         // 0xD000-0xDFFF  switchable on CGB (SVBK)
    EchoRam,           // 0xE000-0xFDFF  mirror of 0xC000-0xDDFF
    Oam,               // 0xFE00-0xFE9F  sprite attributes
    Unusable,          // 0xFEA0-0xFEFF
    IoRegisters,       // 0xFF00-0xFF7F
    Hram,              // 0xFF80-0xFFFE  fast RAM
    InterruptEnable,   // 0xFFFF
};

const char *to_string(MemRegion region);
MemRegion   region_of(u16 addr);

// --- Interrupt sources, in dispatch priority order -------------------------
//  The bit position is also the vector: vector = 0x40 + bit * 8.
enum Interrupt : u8 {
    IntVBlank = 0x01,   // vector 0x40
    IntStat   = 0x02,   // vector 0x48
    IntTimer  = 0x04,   // vector 0x50
    IntSerial = 0x08,   // vector 0x58
    IntJoypad = 0x10,   // vector 0x60
};

// Work RAM: 8 KiB on DMG, 32 KiB on CGB in eight 4 KiB banks (SVBK, 0xFF70).
inline constexpr std::size_t kWramBankSize = 4 * 1024;
inline constexpr std::size_t kWramBanks    = 8;
inline constexpr std::size_t kHramSize     = 127;   // 0xFF80-0xFFFE

class Bus {
public:
    Bus() = default;

    // Takes ownership of the cartridge (decision D3: the Bus owns everything).
    void attach(Cartridge cartridge, Model model);
    void reset();

    bool         has_cartridge() const { return cartridge_.loaded(); }
    Model        model() const { return model_; }
    const char  *model_name() const { return model_ == Model::Cgb ? "CGB" : "DMG"; }

    // --- The only accessors the CPU may use --------------------------------
    // Each call advances the clock by 4 T-cycles BEFORE the access resolves.
    u8   read(u16 addr);
    void write(u16 addr, u8 value);

    // --- Inspection: no clock advance, no side effect ----------------------
    u8 peek(u16 addr) const;

    // Debugger-side write. Same dispatch as write(), but the clock does not
    // advance, so poking a value while single-stepping does not consume time
    // the emulated program never spent.
    void poke(u16 addr, u8 value) { dispatch_write(addr, value); }

    // --- Clock -------------------------------------------------------------
    // `t` is in the CPU domain; components are fed from the domain they
    // belong to (see clock.hpp).
    void tick(u32 t);

    const Clock &clock() const { return clock_; }
    void set_double_speed(bool on) { clock_.set_double_speed(on); }

    // Number of ticking accesses performed, for tests and the debugger.
    u64 access_count() const { return access_count_; }

    // --- Interrupts --------------------------------------------------------
    //  IE lives at 0xFFFF, IF at 0xFF0F. The CPU reads both every step.
    u8   interrupt_enable() const { return interrupt_enable_; }
    u8   interrupt_flags() const  { return static_cast<u8>(0xE0 | io_[0x0F]); }
    void set_interrupt_flags(u8 value) { io_[0x0F] = static_cast<u8>(value & 0x1F); }
    void request_interrupt(Interrupt which) { io_[0x0F] |= static_cast<u8>(which & 0x1F); }

    // --- Serial port -------------------------------------------------------
    //  Not required by the subject (no link cable is mentioned anywhere), but
    //  blargg's test ROMs report their results through it, which is how the
    //  CPU can be validated before any screen exists.
    const std::string &serial_output() const { return serial_; }
    void clear_serial_output() { serial_.clear(); }

    Ppu             &ppu()       { return ppu_; }
    const Ppu       &ppu() const { return ppu_; }
    Cartridge       &cartridge()       { return cartridge_; }
    const Cartridge &cartridge() const { return cartridge_; }

private:
    u8   dispatch_read(u16 addr) const;
    void dispatch_write(u16 addr, u8 value);

    // WRAM bank visible at 0xD000-0xDFFF. Always 1 on DMG; on CGB the SVBK
    // register selects 1-7, and the value 0 is treated as 1 by the hardware.
    std::size_t wram_bank() const;

    Cartridge cartridge_;
    Ppu       ppu_;
    Clock     clock_;
    Model     model_ = Model::Dmg;

    std::array<u8, kWramBankSize * kWramBanks> wram_{};
    std::array<u8, kHramSize>                  hram_{};

    // Placeholder storage for 0xFF00-0xFF7F. Each register moves to its real
    // owner as the peripherals are implemented (timer in step 7, PPU
    // registers in step 8, joypad in step 11).
    std::array<u8, 0x80> io_{};

    std::string serial_;         // everything the game sent over the link port

    u8  interrupt_enable_ = 0;   // 0xFFFF
    u8  svbk_             = 1;   // 0xFF70, CGB WRAM bank select
    u64 access_count_     = 0;
};

}  // namespace retroemu
