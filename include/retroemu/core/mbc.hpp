#pragma once
// ===========================================================================
//  Memory bank controllers — section V.5 of the subject (p.8).
// ===========================================================================
//  The CPU can name 65536 addresses, of which 32768 belong to the cartridge.
//  A cartridge can hold 8 MiB. Something has to reconcile those two numbers,
//  and that something is a chip inside the cartridge itself.
//
//  The trick: the second half of the cartridge window, 0x4000-0x7FFF, is not
//  a fixed 16 KiB of ROM. It is a WINDOW onto whichever 16 KiB bank the chip
//  currently points at. The game moves the window by writing to the cartridge.
//
//  Writing to ROM. Which stores nothing, because ROM is read-only. The bytes
//  reach the chip instead, and it reads them as commands. It is the most
//  counter-intuitive mechanism in the machine, and the reason Bus::write has
//  to route 0x0000-0x7FFF to the cartridge rather than dropping the write.
//
//  The subject requires MBC1, MBC2 and MBC5, plus cartridges with no chip at
//  all. MBC3 is a bonus (Ch. VI, p.9).
// ===========================================================================

#include <memory>
#include <string>
#include <vector>

#include "retroemu/core/types.hpp"

namespace retroemu {

struct CartridgeHeader;

inline constexpr std::size_t kRomBankSize = 16 * 1024;
inline constexpr std::size_t kRamBankSize =  8 * 1024;

class Mbc {
public:
    virtual ~Mbc() = default;

    // 0x0000-0x7FFF
    virtual u8   read_rom(u16 addr) const = 0;
    virtual void write_rom(u16 addr, u8 value) = 0;

    // 0xA000-0xBFFF
    virtual u8   read_ram(u16 addr) const = 0;
    virtual void write_ram(u16 addr, u8 value) = 0;

    virtual const char *name() const = 0;

    // Which bank each window currently shows. For the debugger.
    virtual unsigned rom_bank_low() const { return 0; }
    virtual unsigned rom_bank_high() const { return 1; }
    virtual unsigned ram_bank() const { return 0; }
    virtual bool     ram_enabled() const { return false; }

    std::size_t rom_size() const { return rom_.size(); }

    const std::vector<u8> &ram() const { return ram_; }
    std::vector<u8>       &ram() { return ram_; }

    // True once the game has written to the battery-backed RAM, so a save
    // file is only produced when there is something to save.
    bool ram_written() const { return ram_written_; }

protected:
    Mbc(std::vector<u8> rom, std::size_t ram_size)
        : rom_(std::move(rom)), ram_(ram_size, 0xFF) {}

    // Banks wrap rather than reading past the end: a game asking for bank 40
    // of a four-bank cartridge sees bank 0, which is what the address lines
    // that are simply not connected produce.
    std::size_t rom_banks() const
    {
        return rom_.empty() ? 1 : (rom_.size() + kRomBankSize - 1) / kRomBankSize;
    }
    std::size_t ram_banks() const
    {
        return ram_.empty() ? 0 : (ram_.size() + kRamBankSize - 1) / kRamBankSize;
    }

    u8 rom_byte(std::size_t bank, u16 offset) const
    {
        if (rom_.empty()) return 0xFF;
        const std::size_t index = (bank % rom_banks()) * kRomBankSize + offset;
        return index < rom_.size() ? rom_[index] : 0xFF;
    }

    std::vector<u8> rom_;
    std::vector<u8> ram_;
    bool            ram_written_ = false;
};

// Builds the controller the header describes. Never returns null: an unknown
// type falls back to a flat mapping so the ROM can at least be inspected.
std::unique_ptr<Mbc> make_mbc(const CartridgeHeader &header, std::vector<u8> rom);

}  // namespace retroemu
