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
    // A CGB running a cartridge that knows nothing about colour keeps all the
    // extra hardware but draws in black and white, exactly as the console does
    // (decision D58). The cartridge header is what decides.
    const bool dmg_compatibility =
        model_ == Model::Cgb && cartridge_.header().cgb == CgbSupport::None;
    ppu_.reset(model_, dmg_compatibility);
    timer_.reset(model_);
    dma_.reset();
    joypad_.reset();
    clock_.reset();
    wram_.fill(0);
    hram_.fill(0);
    io_.fill(0);
    serial_.clear();
    interrupt_enable_ = 0;
    svbk_             = 1;
    key1_             = 0;
    access_count_     = 0;
    hdma_source_ = 0;
    hdma_dest_   = 0;
    hdma_length_ = 0;
    hdma_active_ = false;
    hdma_regs_.fill(0xFF);

    // Values the real boot ROM leaves in the I/O registers. The mandatory part
    // skips the boot sequence (ambiguity A3), so they are applied directly.
    io_[0x02] = 0x7E;   // SC
    io_[0x0F] = 0xE1;   // IF
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
// While the sprite copier is running it owns the bus, and the CPU can only
// reach HRAM. That is why games copy their DMA routine into HRAM and run it
// from there: anywhere else, their own instruction fetches would be blocked.
//
// The restriction applies to read() and write() only, never to peek() and
// poke(). Those are how the copier itself moves bytes, and how the debugger
// and the tracer look at memory, none of which the hardware bus arbitration
// concerns.
bool Bus::cpu_blocked_by_dma(u16 addr) const
{
    if (!dma_.active()) return false;
    const bool in_hram = (addr >= 0xFF80 && addr <= 0xFFFE);
    return !in_hram;
}

u8 Bus::read(u16 addr)
{
    tick(kTCyclesPerMCycle);
    ++access_count_;
    if (cpu_blocked_by_dma(addr)) return 0xFF;
    return dispatch_read(addr);
}

void Bus::write(u16 addr, u8 value)
{
    tick(kTCyclesPerMCycle);
    ++access_count_;
    if (cpu_blocked_by_dma(addr)) return;
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

    // Each component is fed from its own domain. The PPU never speeds up, so
    // it gets system cycles. The timer follows the CPU clock, so it gets CPU
    // cycles and does run twice as fast in CGB double-speed mode.
    ppu_.tick(t_sys);
    if (ppu_.take_hblank_entered()) hdma_service_hblank();
    if (ppu_.take_vblank_irq()) request_interrupt(IntVBlank);
    if (ppu_.take_stat_irq())   request_interrupt(IntStat);
    if (timer_.tick(t))         request_interrupt(IntTimer);

    // The copier moves one byte per machine cycle of the CPU clock, so it too
    // runs twice as fast in CGB double-speed mode.
    dma_.tick(*this, t);

    if (joypad_.take_irq()) request_interrupt(IntJoypad);
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
            // The LY stub must be tested BEFORE the PPU is consulted, or the
            // PPU answers first and the stub becomes dead code.
            if (addr == 0xFF44 && ly_stub_) return 0x90;   // see Bus::set_ly_stub
            if (addr >= 0xFF04 && addr <= 0xFF07) return timer_.read(addr);
            if (addr == 0xFF00) return joypad_.read();
            if (addr == kOamDmaRegister) return dma_.source_page();
            if (addr >= 0xFF40 && addr <= 0xFF4B) return ppu_.read(addr);
            if (addr == 0xFF0F) return static_cast<u8>(0xE0 | io_[0x0F]);
            if (model_ == Model::Cgb) {
                if (addr == 0xFF4F) return ppu_.vram_bank_register();
                if (addr == 0xFF70) return svbk_;
                // KEY1: bit 7 is the speed the CPU is running at right now,
                // bit 0 is "a switch has been asked for". Everything else
                // reads as 1.
                if (addr == 0xFF4D) {
                    return static_cast<u8>(0x7E | (clock_.double_speed() ? 0x80 : 0x00)
                                                | (key1_ & 0x01));
                }
                // HDMA1-4 are write-only on the real chip: the source and
                // destination cannot be read back, only HDMA5 reports state.
                if (addr >= 0xFF51 && addr <= 0xFF54) return 0xFF;
                // HDMA5 reports the transfer that is still to come. Bit 7 set
                // means "nothing running"; the low seven bits are the number
                // of 16-byte blocks left, minus one.
                if (addr == 0xFF55) {
                    if (hdma_length_ == 0) return 0xFF;
                    const u8 blocks = static_cast<u8>((hdma_length_ / 16) - 1);
                    return static_cast<u8>((hdma_active_ ? 0x00 : 0x80) | (blocks & 0x7F));
                }
                if (addr >= 0xFF68 && addr <= 0xFF6C) return ppu_.read(addr);
            }
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
void Bus::dispatch_write(u16 addr, u8 value, bool timed)
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
            dispatch_write(static_cast<u16>(addr - 0x2000), value, timed);
            return;

        case MemRegion::Oam:
            ppu_.write_oam(addr, value);
            return;

        case MemRegion::Unusable:
            return;   // writes are dropped

        case MemRegion::IoRegisters:
            if (addr >= 0xFF04 && addr <= 0xFF07) { timer_.write(addr, value); return; }
            if (addr == 0xFF00) { joypad_.write(value); return; }
            if (addr == kOamDmaRegister) { dma_.start(value); return; }
            if (addr >= 0xFF40 && addr <= 0xFF4B) { ppu_.write(addr, value); return; }
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
            if (model_ == Model::Cgb) {
                if (addr == 0xFF4F) { ppu_.set_vram_bank_register(value); return; }
                if (addr == 0xFF70) { svbk_ = value & 0x07; return; }
                if (addr == 0xFF4D) { key1_ = static_cast<u8>(value & 0x01); return; }
                if (addr >= 0xFF51 && addr <= 0xFF54) { hdma_regs_[addr - 0xFF51] = value; return; }
                if (addr == 0xFF55) { hdma_write_control(value, timed); return; }
                if (addr >= 0xFF68 && addr <= 0xFF6C) { ppu_.write(addr, value); return; }
            }
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

// ---------------------------------------------------------------------------
//  CGB speed switch (KEY1, 0xFF4D)
// ---------------------------------------------------------------------------
//  A game writes 1 to KEY1 and then executes STOP. The console does not stop:
//  it changes the CPU's clock and carries on. This is the whole reason the
//  clock was split into two domains back in step 3 — the CPU and the timer
//  now run twice as fast while the PPU keeps refreshing the screen 59.727
//  times a second.
//
//  The switch is not free: the machine is frozen for about 2050 machine
//  cycles while the clock settles, and the divider is reset.
// ---------------------------------------------------------------------------
void Bus::perform_speed_switch()
{
    if (!speed_switch_armed()) return;

    key1_ = 0;                      // the request is consumed
    timer_.write(0xFF04, 0);        // writing DIV resets the internal counter
    clock_.set_double_speed(!clock_.double_speed());
    tick(2050 * kTCyclesPerMCycle); // the pause the hardware takes
}

// ---------------------------------------------------------------------------
//  CGB VRAM DMA (0xFF51-0xFF55)
// ---------------------------------------------------------------------------
//  The OAM copier of step 10 moves 160 bytes into the sprite table. This one
//  moves up to 2 KiB into VIDEO memory, which is what makes full-screen
//  animation possible on a CGB, and it comes in two flavours:
//
//    general purpose  the whole block at once. The CPU is frozen throughout.
//    HBlank           16 bytes at the start of every HBlank. The CPU keeps
//                     running in between, and the transfer spreads itself
//                     over as many scanlines as it needs.
//
//  HBlank mode is the interesting one: it is the only way to push a large
//  amount of data into VRAM without giving up a whole frame, because it uses
//  the small gaps the PPU leaves between scanlines.
// ---------------------------------------------------------------------------
void Bus::hdma_transfer_block()
{
    // peek/poke, never read/write: the copier is not the CPU, so it must not
    // charge its accesses to the clock a second time (decision D15).
    for (int i = 0; i < 16; ++i) {
        const u8 byte = peek(static_cast<u16>(hdma_source_ + i));
        // The destination always lands inside VRAM, whatever was written.
        poke(static_cast<u16>(0x8000 | ((hdma_dest_ + i) & 0x1FFF)), byte);
    }
    hdma_source_ = static_cast<u16>(hdma_source_ + 16);
    hdma_dest_   = static_cast<u16>(hdma_dest_ + 16);
    hdma_length_ = static_cast<u16>(hdma_length_ - 16);
    if (hdma_length_ == 0) hdma_active_ = false;
}

void Bus::hdma_service_hblank()
{
    if (!hdma_active_ || hdma_length_ == 0) return;
    hdma_transfer_block();
}

void Bus::hdma_write_control(u8 value, bool timed)
{
    const u16 length = static_cast<u16>(((value & 0x7F) + 1) * 16);

    if ((value & 0x80) == 0) {
        // Bit 7 clear has two meanings, and which one applies depends on
        // whether an HBlank transfer is already running.
        if (hdma_active_) {
            // Cancel it. What is left stays readable in HDMA5, so a game can
            // pick the transfer up again later.
            hdma_active_ = false;
            return;
        }

        // General purpose: everything moves now.
        hdma_source_ = static_cast<u16>((hdma_regs_[0] << 8) | (hdma_regs_[1] & 0xF0));
        hdma_dest_   = static_cast<u16>(((hdma_regs_[2] & 0x1F) << 8) | (hdma_regs_[3] & 0xF0));
        hdma_length_ = length;
        while (hdma_length_ > 0) {
            hdma_transfer_block();
            // The CPU is frozen while this happens, so the time has to be
            // charged: two bytes per machine cycle at normal speed, one per
            // machine cycle at double speed.
            if (timed) tick(kTCyclesPerMCycle * (clock_.double_speed() ? 16 : 8));
        }
        hdma_active_ = false;
        return;
    }

    // HBlank mode: arm it and let the PPU drive it, 16 bytes at a time.
    hdma_source_ = static_cast<u16>((hdma_regs_[0] << 8) | (hdma_regs_[1] & 0xF0));
    hdma_dest_   = static_cast<u16>(((hdma_regs_[2] & 0x1F) << 8) | (hdma_regs_[3] & 0xF0));
    hdma_length_ = length;
    hdma_active_ = true;
}

}  // namespace retroemu
