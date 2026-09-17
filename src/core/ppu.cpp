#include "retroemu/core/ppu.hpp"

namespace retroemu {

void Ppu::reset()
{
    vram_.fill(0);
    oam_.fill(0);
    vram_bank_ = 0;
    elapsed_   = 0;
}

u8 Ppu::read_vram(u16 addr) const
{
    const std::size_t offset = static_cast<std::size_t>(addr - 0x8000);
    return vram_[vram_bank_ * kVramBankSize + offset];
}

void Ppu::write_vram(u16 addr, u8 value)
{
    const std::size_t offset = static_cast<std::size_t>(addr - 0x8000);
    vram_[vram_bank_ * kVramBankSize + offset] = value;
}

u8 Ppu::read_oam(u16 addr) const
{
    return oam_[static_cast<std::size_t>(addr - 0xFE00)];
}

void Ppu::write_oam(u16 addr, u8 value)
{
    oam_[static_cast<std::size_t>(addr - 0xFE00)] = value;
}

// Only bit 0 of VBK is wired; reads return the other bits set.
u8 Ppu::vram_bank_register() const { return static_cast<u8>(0xFE | vram_bank_); }

void Ppu::set_vram_bank_register(u8 value) { vram_bank_ = value & 0x01; }

void Ppu::tick(u32 t_sys)
{
    // Step 8 turns this into the mode 2 / 3 / 0 / 1 state machine. For now the
    // PPU only proves that it is being fed from the system clock domain.
    elapsed_ += t_sys;
}

}  // namespace retroemu
