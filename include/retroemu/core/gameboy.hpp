#pragma once
// ===========================================================================
//  GameBoy — the assembled machine.
// ===========================================================================
//  A thin facade over the Bus and the Cpu, so that the frontend, the debugger
//  and the headless runner all drive the emulation through one interface
//  instead of wiring the components together three times.
// ===========================================================================

#include <string>

#include "retroemu/core/bus.hpp"
#include "retroemu/core/cpu.hpp"
#include "retroemu/core/types.hpp"

namespace retroemu {

class GameBoy {
public:
    // Loads a cartridge and resets. `force_cgb` overrides the model that the
    // header asks for (the "forcing DMG/CGB" bonus, subject Ch. VI).
    bool load(const std::string &path, bool force_cgb, std::string &error);

    // Same, from bytes already in memory.
    bool load_from_memory(std::vector<u8> rom, const std::string &name,
                          bool force_cgb, std::string &error);

    void reset();

    // Execute one instruction. Returns the T-cycles it consumed.
    u32 step();

    // Run until the SYSTEM clock has advanced by `t_sys` cycles. The system
    // domain is used rather than the CPU one so that a frame still lasts a
    // frame in CGB double-speed mode.
    void run_system_cycles(u64 t_sys);

    // One frame is 70224 system cycles, which is 59.727 frames per second.
    // Step 8 replaces this with "run until the PPU signals VBlank".
    void run_frame() { run_system_cycles(kTCyclesPerFrame); }
    void run_seconds(double seconds);

    bool  loaded() const { return bus_.has_cartridge(); }
    Model model() const  { return model_; }

    Bus       &bus()       { return bus_; }
    const Bus &bus() const { return bus_; }
    Cpu       &cpu()       { return cpu_; }
    const Cpu &cpu() const { return cpu_; }

private:
    Bus   bus_;
    Cpu   cpu_;
    Model model_ = Model::Dmg;
};

}  // namespace retroemu
