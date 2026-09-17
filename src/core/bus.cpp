#include "retroemu/core/bus.hpp"

namespace retroemu {

// ---------------------------------------------------------------------------
//  The address map
// ---------------------------------------------------------------------------
const char *to_string(MemRegion region)
{
    switch (region) {
        case MemRegion::RomBank0:        return "ROM bank 0";
        case MemRegion::RomBankN:        return "ROM bank N";
        case MemRegion::Vram:            return "VRAM";
        case MemRegion::ExternalRam:     return "external RAM";
        case MemRegion::WramBank0:       return "WRAM bank 0";
        case MemRegion::WramBankN:       return "WRAM bank N";
        case MemRegion::EchoRam:         return "echo RAM";
        case MemRegion::Oam:             return "OAM";
        case MemRegion::Unusable:        return "unusable";
        case MemRegion::IoRegisters:     return "I/O registers";
        case MemRegion::Hram:            return "HRAM";
        case MemRegion::InterruptEnable: return "IE register";
    }
    return "?";
}

MemRegion region_of(u16 addr)
{
    if (addr < 0x4000) return MemRegion::RomBank0;
    if (addr < 0x8000) return MemRegion::RomBankN;
    if (addr < 0xA000) return MemRegion::Vram;
    if (addr < 0xC000) return MemRegion::ExternalRam;
    if (addr < 0xD000) return MemRegion::WramBank0;
    if (addr < 0xE000) return MemRegion::WramBankN;
    if (addr < 0xFE00) return MemRegion::EchoRam;
    if (addr < 0xFEA0) return MemRegion::Oam;
    if (addr < 0xFF00) return MemRegion::Unusable;
    if (addr < 0xFF80) return MemRegion::IoRegisters;
    if (addr < 0xFFFF) return MemRegion::Hram;
    return MemRegion::InterruptEnable;
}

// ---------------------------------------------------------------------------
//  Setup
// ---------------------------------------------------------------------------
void Bus::attach(Cartridge cartridge, Model model)
{
    cartridge_ = std::move(cartridge);
    model_     = model;
    reset();
}

void Bus::reset()
{
    ppu_.reset();
    clock_.reset();
    wram_.fill(0);
    hram_.fill(0);
    io_.fill(0);
    serial_.clear();
    interrupt_enable_ = 0;
    svbk_             = 1;
    access_count_     = 0;

    // Values the real boot ROM leaves in the I/O registers. The mandatory part
    // skips the boot sequence (ambiguity A3), so they are applied directly.
    io_[0x00] = 0xCF;   // JOYP: nothing pressed
    io_[0x02] = 0x7E;   // SC
    io_[0x07] = 0xF8;   // TAC
    io_[0x0F] = 0xE1;   // IF
    io_[0x40] = 0x91;   // LCDC: screen on, background enabled
    io_[0x41] = 0x85;   // STAT
    io_[0x47] = 0xFC;   // BGP
    io_[0x48] = 0xFF;   // OBP0
    io_[0x49] = 0xFF;   // OBP1
}

std::size_t Bus::wram_bank() const
{
    if (model_ != Model::Cgb) return 1;
    const std::size_t bank = svbk_ & 0x07;
    return bank == 0 ? 1 : bank;   // hardware reads bank 0 as bank 1
}

// ---------------------------------------------------------------------------
//  The ticking accessors
// ---------------------------------------------------------------------------
//  The clock advances BEFORE the access resolves, which is what makes the
//  timer and the PPU see the access at the right moment (decision D8).
// ---------------------------------------------------------------------------
u8 Bus::read(u16 addr)
{
    tick(kTCyclesPerMCycle);
    ++access_count_;
    return dispatch_read(addr);
}

void Bus::write(u16 addr, u8 value)
{
    tick(kTCyclesPerMCycle);
    ++access_count_;
    dispatch_write(addr, value);
}

u8 Bus::peek(u16 addr) const
{
    return dispatch_read(addr);
}

void Bus::tick(u32 t)
{
    // The clock converts CPU cycles into system cycles once, and tells us how
    // many elapsed. Each component is then fed from the domain it belongs to.
    const u32 t_sys = clock_.advance(t);

    // The PPU never speeds up, so it gets system cycles, not CPU cycles.
    // Steps 7 and 10 add timer_.tick(t) and dma_.tick(t_sys) here.
    ppu_.tick(t_sys);
}

// ---------------------------------------------------------------------------
//  Read dispatch
// ---------------------------------------------------------------------------
u8 Bus::dispatch_read(u16 addr) const
{
    switch (region_of(addr)) {
        case MemRegion::RomBank0:
        case MemRegion::RomBankN:
        case MemRegion::ExternalRam:
            return cartridge_.read(addr);

        case MemRegion::Vram:
            return ppu_.read_vram(addr);

        case MemRegion::WramBank0:
            return wram_[addr - 0xC000];

        case MemRegion::WramBankN:
            return wram_[wram_bank() * kWramBankSize + (addr - 0xD000)];

        // Echo RAM mirrors 0xC000-0xDDFF. A hardware quirk nobody designed on
        // purpose, but some games do read through it, so it must work.
        case MemRegion::EchoRam:
            return dispatch_read(static_cast<u16>(addr - 0x2000));

        case MemRegion::Oam:
            return ppu_.read_oam(addr);

        // Behaviour here depends on the console revision and on what the PPU
        // is doing. No ROM in the test bundle exercises it, so we return a
        // constant and will revisit if a test ever demands otherwise.
        case MemRegion::Unusable:
            return 0xFF;

        case MemRegion::IoRegisters:
            if (addr == 0xFF0F) return static_cast<u8>(0xE0 | io_[0x0F]);
            if (addr == 0xFF4F && model_ == Model::Cgb) return ppu_.vram_bank_register();
            if (addr == 0xFF70 && model_ == Model::Cgb) return svbk_;
            // Every other register is still a plain byte; steps 7, 8 and 11
            // route them to the timer, the PPU and the joypad.
            return io_[addr - 0xFF00];

        case MemRegion::Hram:
            return hram_[addr - 0xFF80];

        case MemRegion::InterruptEnable:
            return interrupt_enable_;
    }
    return 0xFF;
}

// ---------------------------------------------------------------------------
//  Write dispatch
// ---------------------------------------------------------------------------
void Bus::dispatch_write(u16 addr, u8 value)
{
    switch (region_of(addr)) {
        // Writing "into ROM" stores nothing. The bytes reach the cartridge's
        // controller instead, which reads them as a bank-switch command. The
        // MBCs implement that in step 13.
        case MemRegion::RomBank0:
        case MemRegion::RomBankN:
        case MemRegion::ExternalRam:
            cartridge_.write(addr, value);
            return;

        case MemRegion::Vram:
            ppu_.write_vram(addr, value);
            return;

        case MemRegion::WramBank0:
            wram_[addr - 0xC000] = value;
            return;

        case MemRegion::WramBankN:
            wram_[wram_bank() * kWramBankSize + (addr - 0xD000)] = value;
            return;

        case MemRegion::EchoRam:
            dispatch_write(static_cast<u16>(addr - 0x2000), value);
            return;

        case MemRegion::Oam:
            ppu_.write_oam(addr, value);
            return;

        case MemRegion::Unusable:
            return;   // writes are dropped

        case MemRegion::IoRegisters:
            if (addr == 0xFF01) { io_[0x01] = value; return; }        // SB: byte to send
            if (addr == 0xFF02) {                                       // SC: control
                io_[0x02] = value;
                // Bit 7 means "start transfer". With no cable attached the
                // byte goes nowhere on real hardware; we capture it so test
                // ROMs can report their results.
                if (value & 0x80) {
                    serial_ += static_cast<char>(io_[0x01]);
                    io_[0x02] = static_cast<u8>(value & 0x7F);   // transfer done
                    request_interrupt(IntSerial);
                }
                return;
            }
            if (addr == 0xFF0F) { io_[0x0F] = static_cast<u8>(value & 0x1F); return; }
            if (addr == 0xFF4F && model_ == Model::Cgb) { ppu_.set_vram_bank_register(value); return; }
            if (addr == 0xFF70 && model_ == Model::Cgb) { svbk_ = value & 0x07; return; }
            io_[addr - 0xFF00] = value;
            return;

        case MemRegion::Hram:
            hram_[addr - 0xFF80] = value;
            return;

        case MemRegion::InterruptEnable:
            interrupt_enable_ = value;
            return;
    }
}

}  // namespace retroemu
