#pragma once
// ===========================================================================
//  Cartridge: ROM loading and header parsing.
// ===========================================================================
//  Every cartridge carries a 336-byte header at 0x0100-0x014F describing what
//  the hardware inside it is. Three bytes of that header decide the whole
//  shape of the emulator:
//
//      0x0143  CGB flag       -> DMG or CGB behaviour        (subject V.6)
//      0x0147  cartridge type -> which MBC, RAM, battery     (subject V.5)
//      0x0148  ROM size       -> how many 16 KiB banks exist (subject V.5)
//
//  This step only READS the header. Actually mapping the banks is the job of
//  the MBC implementations in step 13.
// ===========================================================================

#include <memory>
#include <string>
#include <vector>

#include "retroemu/core/mbc.hpp"

#include "retroemu/core/types.hpp"

namespace retroemu {

// --- Memory bank controller family ----------------------------------------
enum class MbcType {
    None,          // no controller at all: a flat 32 KiB ROM
    Mbc1,          // mandatory (subject V.5)
    Mbc2,          // mandatory (subject V.5)
    Mbc3,          // bonus (subject Ch. VI)
    Mbc5,          // mandatory (subject V.5)
    Mbc6,
    Mbc7,
    Mmm01,
    HuC1,
    HuC3,
    PocketCamera,
    BandaiTama5,
    Unknown,
};

// --- Colour model declared by the cartridge (header byte 0x0143) -----------
enum class CgbSupport {
    None,          // value < 0x80: DMG cartridge, the byte is part of the title
    Enhanced,      // 0x80: uses CGB features but still runs on a DMG
    Only,          // 0xC0: refuses to run on a DMG
};

// --- Everything the header tells us ----------------------------------------
struct CartridgeHeader {
    std::string title;              // 0x0134..0x0142/0x0143
    std::string manufacturer_code;  // 0x013F..0x0142, newer cartridges only
    std::string licensee_code;      // 0x0144..0x0145 (new) or 0x014B (old)

    CgbSupport  cgb        = CgbSupport::None;   // 0x0143
    bool        sgb        = false;              // 0x0146 == 0x03

    u8          type_code  = 0;                  // 0x0147, raw value
    MbcType     mbc        = MbcType::None;
    bool        has_ram    = false;
    bool        has_battery = false;
    bool        has_timer  = false;              // real time clock (MBC3)
    bool        has_rumble = false;              // MBC5/MBC7

    u8          rom_size_code = 0;               // 0x0148
    std::size_t rom_size      = 0;               // in bytes
    unsigned    rom_banks     = 0;               // 16 KiB each

    u8          ram_size_code = 0;               // 0x0149
    std::size_t ram_size      = 0;               // in bytes
    unsigned    ram_banks     = 0;               // 8 KiB each

    u8          destination_code = 0;            // 0x014A: 0 = Japan
    u8          rom_version      = 0;            // 0x014C

    u8          header_checksum          = 0;    // 0x014D, as stored
    u8          computed_header_checksum = 0;
    u16         global_checksum          = 0;    // 0x014E..0x014F, big-endian
    u16         computed_global_checksum = 0;

    // Non-fatal oddities worth reporting (size mismatch, bad checksum, ...).
    std::vector<std::string> warnings;

    bool header_checksum_valid() const { return header_checksum == computed_header_checksum; }
    bool global_checksum_valid() const { return global_checksum == computed_global_checksum; }
};

// --- Human-readable names --------------------------------------------------
const char *to_string(MbcType mbc);
const char *to_string(CgbSupport cgb);

// Full name of a raw 0x0147 value, e.g. 0x03 -> "MBC1+RAM+BATTERY".
const char *cartridge_type_name(u8 type_code);

// True for the controllers this emulator is required to support
// (subject V.5: ROM only, MBC1, MBC2, MBC5).
bool is_mandatory_mbc(MbcType mbc);

// --- Checksums -------------------------------------------------------------
//  Header checksum, over 0x0134..0x014C:
//      checksum = 0; for each byte: checksum = checksum - byte - 1
//  On real hardware the boot ROM halts the console when this does not match.
u8 compute_header_checksum(const std::vector<u8> &rom);

//  Global checksum: 16-bit sum of every ROM byte except the two checksum
//  bytes themselves. Real hardware never verifies it.
u16 compute_global_checksum(const std::vector<u8> &rom);

// --- Parsing ---------------------------------------------------------------
//  Returns false and fills `error` when the data cannot be a cartridge at all.
//  Recoverable oddities land in `out.warnings` instead.
bool parse_header(const std::vector<u8> &rom, CartridgeHeader &out, std::string &error);

// ---------------------------------------------------------------------------
//  A loaded cartridge: the raw ROM bytes plus its parsed header.
// ---------------------------------------------------------------------------
class Cartridge {
public:
    // Returns false and fills `error` on failure. The object is left empty.
    bool load_from_file(const std::string &path, std::string &error);

    // Same, from bytes already in memory. Used by the built-in CPU self-test,
    // which must run without any external ROM file.
    bool load_from_memory(std::vector<u8> rom, const std::string &name, std::string &error);

    // --- Access from the bus ------------------------------------------------
    //  0x0000-0x7FFF goes to the controller, which decides which bank each
    //  window shows. 0xA000-0xBFFF is the cartridge's own RAM.
    u8   read(u16 addr) const;
    void write(u16 addr, u8 value);

    // How many bank-switch commands the controller has received.
    u64 bank_commands_seen() const { return bank_commands_; }

    const Mbc *mbc() const { return mbc_.get(); }

    // --- Battery-backed RAM (subject V.5, p.8) ------------------------------
    //  "You must also manage the in-game backup for games that propose this
    //   feature (battery-backed cartridge RAM persisted on disk between
    //   sessions)."
    bool has_battery() const { return header_.has_battery && header_.ram_size > 0; }

    // Where the save lives: the cartridge's path with its extension replaced.
    std::string save_path() const;

    // Both are silent no-ops for a cartridge with no battery. load_battery is
    // called automatically when a cartridge is loaded; save_battery has to be
    // asked for, because when to write is the caller's business.
    bool load_battery();
    bool save_battery() const;

    // Set by load_battery when the file on disk is not the size this
    // cartridge's RAM expects. The save is still loaded — refusing it would
    // throw away a player's progress — but the mismatch is reported rather
    // than swallowed, because it usually means the wrong .sav next to the
    // wrong ROM. Empty when there is nothing to say.
    const std::string &save_note() const { return save_note_; }

    std::size_t rom_size() const { return mbc_ ? mbc_->rom_size() : 0; }
    const std::vector<u8> &ram() const;

    bool                    loaded() const { return mbc_ != nullptr; }
    const std::string      &path()   const { return path_; }
    const CartridgeHeader  &header() const { return header_; }

private:
    std::string          path_;
    std::unique_ptr<Mbc> mbc_;         // owns the ROM and the cartridge RAM
    CartridgeHeader      header_;
    std::string          save_note_;
    u64                  bank_commands_ = 0;
};

}  // namespace retroemu
