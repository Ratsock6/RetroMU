#include "retroemu/core/mbc.hpp"

#include "retroemu/core/cartridge.hpp"

namespace retroemu {
namespace {

// ---------------------------------------------------------------------------
//  No controller: a flat 32 KiB, optionally with a little RAM.
// ---------------------------------------------------------------------------
class NoMbc final : public Mbc {
public:
    NoMbc(std::vector<u8> rom, std::size_t ram_size) : Mbc(std::move(rom), ram_size) {}

    u8 read_rom(u16 addr) const override
    {
        return addr < rom_.size() ? rom_[addr] : 0xFF;
    }

    void write_rom(u16, u8) override {}   // there is no chip to talk to

    u8 read_ram(u16 addr) const override
    {
        const std::size_t offset = static_cast<std::size_t>(addr - 0xA000);
        return offset < ram_.size() ? ram_[offset] : 0xFF;
    }

    void write_ram(u16 addr, u8 value) override
    {
        const std::size_t offset = static_cast<std::size_t>(addr - 0xA000);
        if (offset < ram_.size()) { ram_[offset] = value; ram_written_ = true; }
    }

    const char *name() const override { return "none"; }
    bool ram_enabled() const override { return !ram_.empty(); }
};

// ---------------------------------------------------------------------------
//  MBC1 — up to 2 MiB of ROM and 32 KiB of RAM.
// ---------------------------------------------------------------------------
//  Two bank registers rather than one, and a mode bit that decides what the
//  second one means. That mode bit is where the quirks live.
//
//  Writing 0 to the 5-bit register selects bank 1, not bank 0. There is no
//  way to put bank 0 in the switchable window, and as a consequence banks
//  0x20, 0x40 and 0x60 are unreachable there too on a large cartridge.
// ---------------------------------------------------------------------------
class Mbc1 final : public Mbc {
public:
    Mbc1(std::vector<u8> rom, std::size_t ram_size) : Mbc(std::move(rom), ram_size) {}

    u8 read_rom(u16 addr) const override
    {
        if (addr < 0x4000) return rom_byte(low_bank(), addr);
        return rom_byte(high_bank(), static_cast<u16>(addr - 0x4000));
    }

    void write_rom(u16 addr, u8 value) override
    {
        if (addr < 0x2000) {
            // Any value whose low nibble is 0x0A opens the RAM. Anything else
            // closes it, which is how a game protects a save from a crash.
            ram_enabled_ = (value & 0x0F) == 0x0A;
        } else if (addr < 0x4000) {
            bank1_ = value & 0x1F;
            if (bank1_ == 0) bank1_ = 1;   // 0 means 1; bank 0 is unreachable here
        } else if (addr < 0x6000) {
            bank2_ = value & 0x03;
        } else {
            mode_ = (value & 0x01) != 0;
        }
    }

    u8 read_ram(u16 addr) const override
    {
        if (!ram_enabled_ || ram_.empty()) return 0xFF;
        const std::size_t offset = ram_offset(addr);
        return offset < ram_.size() ? ram_[offset] : 0xFF;
    }

    void write_ram(u16 addr, u8 value) override
    {
        if (!ram_enabled_ || ram_.empty()) return;
        const std::size_t offset = ram_offset(addr);
        if (offset < ram_.size()) { ram_[offset] = value; ram_written_ = true; }
    }

    const char *name() const override { return "MBC1"; }
    unsigned rom_bank_low() const override { return static_cast<unsigned>(low_bank()); }
    unsigned rom_bank_high() const override { return static_cast<unsigned>(high_bank()); }
    unsigned ram_bank() const override { return mode_ ? bank2_ : 0u; }
    bool     ram_enabled() const override { return ram_enabled_; }

private:
    // In mode 0 the fixed window is always bank 0. In mode 1 the second
    // register reaches it too, which is what lets a 2 MiB cartridge show
    // banks 0x20, 0x40 and 0x60 down there.
    std::size_t low_bank() const
    {
        return mode_ ? static_cast<std::size_t>(bank2_) << 5 : 0;
    }

    std::size_t high_bank() const
    {
        return (static_cast<std::size_t>(bank2_) << 5) | bank1_;
    }

    std::size_t ram_offset(u16 addr) const
    {
        // The bank number is wrapped by the number of banks that actually
        // exist. A cartridge with a single 8 KiB chip has no wires for the
        // bank bits, so selecting bank 1, 2 or 3 shows bank 0 again rather
        // than reading nothing.
        const std::size_t banks = ram_banks();
        const std::size_t bank  = (mode_ && banks) ? (bank2_ % banks) : 0;
        return bank * kRamBankSize + static_cast<std::size_t>(addr - 0xA000);
    }

    u8   bank1_       = 1;
    u8   bank2_       = 0;
    bool mode_        = false;
    bool ram_enabled_ = false;
};

// ---------------------------------------------------------------------------
//  MBC2 — small, and the odd one out.
// ---------------------------------------------------------------------------
//  Its RAM is inside the chip: 512 half-bytes. Only the low four bits of each
//  exist, so reads return the other four as ones, and the header announces no
//  RAM at all.
//
//  Stranger still, it has no separate address range for its two commands.
//  Bit 8 of the ADDRESS written to decides which one is meant.
// ---------------------------------------------------------------------------
class Mbc2 final : public Mbc {
public:
    explicit Mbc2(std::vector<u8> rom) : Mbc(std::move(rom), 512) {}

    u8 read_rom(u16 addr) const override
    {
        if (addr < 0x4000) return rom_byte(0, addr);
        return rom_byte(bank_, static_cast<u16>(addr - 0x4000));
    }

    void write_rom(u16 addr, u8 value) override
    {
        if (addr >= 0x4000) return;

        if ((addr & 0x0100) != 0) {
            bank_ = value & 0x0F;
            if (bank_ == 0) bank_ = 1;
        } else {
            ram_enabled_ = (value & 0x0F) == 0x0A;
        }
    }

    u8 read_ram(u16 addr) const override
    {
        if (!ram_enabled_) return 0xFF;
        // 512 half-bytes echoed across the whole 8 KiB window.
        return static_cast<u8>(0xF0 | (ram_[(addr - 0xA000) & 0x01FF] & 0x0F));
    }

    void write_ram(u16 addr, u8 value) override
    {
        if (!ram_enabled_) return;
        ram_[(addr - 0xA000) & 0x01FF] = static_cast<u8>(value & 0x0F);
        ram_written_ = true;
    }

    const char *name() const override { return "MBC2"; }
    unsigned rom_bank_high() const override { return bank_; }
    bool     ram_enabled() const override { return ram_enabled_; }

private:
    u8   bank_        = 1;
    bool ram_enabled_ = false;
};

// ---------------------------------------------------------------------------
//  MBC5 — the biggest and the simplest.
// ---------------------------------------------------------------------------
//  Up to 8 MiB of ROM and 128 KiB of RAM. The bank number is nine bits split
//  across two registers, and unlike MBC1 bank 0 CAN be selected in the
//  switchable window: writing 0 means bank 0, not bank 1.
// ---------------------------------------------------------------------------
class Mbc5 final : public Mbc {
public:
    Mbc5(std::vector<u8> rom, std::size_t ram_size) : Mbc(std::move(rom), ram_size) {}

    u8 read_rom(u16 addr) const override
    {
        if (addr < 0x4000) return rom_byte(0, addr);
        return rom_byte(bank_, static_cast<u16>(addr - 0x4000));
    }

    void write_rom(u16 addr, u8 value) override
    {
        if (addr < 0x2000) {
            ram_enabled_ = (value & 0x0F) == 0x0A;
        } else if (addr < 0x3000) {
            bank_ = static_cast<u16>((bank_ & 0x100) | value);   // low eight bits
        } else if (addr < 0x4000) {
            bank_ = static_cast<u16>((bank_ & 0x0FF) | ((value & 0x01) << 8));   // ninth bit
        } else if (addr < 0x6000) {
            // Bit 3 drives the rumble motor on cartridges that have one, and
            // is not part of the bank number there. With no motor to drive,
            // the four bits are simply masked to the banks that exist.
            ram_bank_ = value & 0x0F;
        }
    }

    u8 read_ram(u16 addr) const override
    {
        if (!ram_enabled_ || ram_.empty()) return 0xFF;
        const std::size_t offset = ram_offset(addr);
        return offset < ram_.size() ? ram_[offset] : 0xFF;
    }

    void write_ram(u16 addr, u8 value) override
    {
        if (!ram_enabled_ || ram_.empty()) return;
        const std::size_t offset = ram_offset(addr);
        if (offset < ram_.size()) { ram_[offset] = value; ram_written_ = true; }
    }

    const char *name() const override { return "MBC5"; }
    unsigned rom_bank_high() const override { return bank_; }
    unsigned ram_bank() const override { return ram_bank_; }
    bool     ram_enabled() const override { return ram_enabled_; }

private:
    std::size_t ram_offset(u16 addr) const
    {
        const std::size_t banks = ram_banks();
        const std::size_t bank  = banks ? (ram_bank_ % banks) : 0;
        return bank * kRamBankSize + static_cast<std::size_t>(addr - 0xA000);
    }

    u16  bank_        = 1;
    u8   ram_bank_    = 0;
    bool ram_enabled_ = false;
};

}  // namespace

std::unique_ptr<Mbc> make_mbc(const CartridgeHeader &header, std::vector<u8> rom)
{
    switch (header.mbc) {
        case MbcType::Mbc1: return std::make_unique<Mbc1>(std::move(rom), header.ram_size);
        case MbcType::Mbc2: return std::make_unique<Mbc2>(std::move(rom));
        case MbcType::Mbc5: return std::make_unique<Mbc5>(std::move(rom), header.ram_size);
        case MbcType::None: return std::make_unique<NoMbc>(std::move(rom), header.ram_size);
        default:
            // Unsupported controllers fall back to a flat mapping rather than
            // refusing the cartridge: the header can still be inspected and
            // the first 32 KiB still runs.
            return std::make_unique<NoMbc>(std::move(rom), header.ram_size);
    }
}

}  // namespace retroemu
