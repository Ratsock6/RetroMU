#include "retroemu/core/cartridge.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <vector>

namespace retroemu {
namespace {

// --- Header offsets, straight from the hardware documentation --------------
constexpr std::size_t kHeaderStart      = 0x0100;
constexpr std::size_t kLogoStart        = 0x0104;   // 48 bytes, see note below
constexpr std::size_t kTitleStart       = 0x0134;
constexpr std::size_t kManufacturerCode = 0x013F;
constexpr std::size_t kCgbFlag          = 0x0143;
constexpr std::size_t kNewLicensee      = 0x0144;
constexpr std::size_t kSgbFlag          = 0x0146;
constexpr std::size_t kCartridgeType    = 0x0147;
constexpr std::size_t kRomSize          = 0x0148;
constexpr std::size_t kRamSize          = 0x0149;
constexpr std::size_t kDestination      = 0x014A;
constexpr std::size_t kOldLicensee      = 0x014B;
constexpr std::size_t kRomVersion       = 0x014C;
constexpr std::size_t kHeaderChecksum   = 0x014D;
constexpr std::size_t kGlobalChecksum   = 0x014E;
constexpr std::size_t kHeaderEnd        = 0x0150;   // first byte past the header

// NOTE on the logo at 0x0104..0x0133.
// Real hardware compares those 48 bytes against a copy stored in the boot ROM
// and refuses to run the cartridge when they differ. We deliberately do NOT
// perform that check: it would require embedding the logo bitmap in this
// repository, and the subject (Ch. VI, p.9) states that this artwork is
// protected and must not be shipped. Since the mandatory part skips the boot
// ROM entirely (see docs/decisions.md, A3), nothing depends on it.

constexpr std::size_t kBankSizeRom = 16 * 1024;   // 16 KiB

// The largest cartridge that ever existed holds 512 banks of 16 KiB: 8 MiB,
// on MBC5. Nothing bigger is a cartridge.
//
// This constant is not decoration. A DIRECTORY opens successfully on Linux
// and then reports a size of LONG_MAX, so without a ceiling the loader asks
// for an eight-exabyte allocation and the program dies on std::bad_alloc
// before any of our own error handling is reached.
constexpr std::streamoff kMaxRomSize = 8 * 1024 * 1024;
constexpr std::size_t kBankSizeRam = 8 * 1024;    //  8 KiB

bool is_printable(u8 c) { return c >= 0x20 && c < 0x7F; }

// Read a fixed-width ASCII field, stopping at the first NUL or padding byte.
std::string read_text(const std::vector<u8> &rom, std::size_t start, std::size_t length)
{
    std::string s;
    for (std::size_t i = 0; i < length; ++i) {
        const u8 c = rom[start + i];
        if (c == 0x00) break;
        if (!is_printable(c)) break;
        s += static_cast<char>(c);
    }
    // Trim trailing spaces: titles are space-padded on many cartridges.
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// Decode the 0x0147 byte into the individual hardware features it implies.
void decode_type(u8 code, CartridgeHeader &h)
{
    h.mbc         = MbcType::Unknown;
    h.has_ram     = false;
    h.has_battery = false;
    h.has_timer   = false;
    h.has_rumble  = false;

    switch (code) {
        case 0x00: h.mbc = MbcType::None; break;
        case 0x08: h.mbc = MbcType::None; h.has_ram = true; break;
        case 0x09: h.mbc = MbcType::None; h.has_ram = true; h.has_battery = true; break;

        case 0x01: h.mbc = MbcType::Mbc1; break;
        case 0x02: h.mbc = MbcType::Mbc1; h.has_ram = true; break;
        case 0x03: h.mbc = MbcType::Mbc1; h.has_ram = true; h.has_battery = true; break;

        // MBC2 carries 512 x 4 bits of RAM inside the controller itself, so
        // the RAM size byte at 0x0149 reads 0x00 even though RAM exists.
        case 0x05: h.mbc = MbcType::Mbc2; h.has_ram = true; break;
        case 0x06: h.mbc = MbcType::Mbc2; h.has_ram = true; h.has_battery = true; break;

        case 0x0B: h.mbc = MbcType::Mmm01; break;
        case 0x0C: h.mbc = MbcType::Mmm01; h.has_ram = true; break;
        case 0x0D: h.mbc = MbcType::Mmm01; h.has_ram = true; h.has_battery = true; break;

        case 0x0F: h.mbc = MbcType::Mbc3; h.has_timer = true; h.has_battery = true; break;
        case 0x10: h.mbc = MbcType::Mbc3; h.has_timer = true; h.has_ram = true; h.has_battery = true; break;
        case 0x11: h.mbc = MbcType::Mbc3; break;
        case 0x12: h.mbc = MbcType::Mbc3; h.has_ram = true; break;
        case 0x13: h.mbc = MbcType::Mbc3; h.has_ram = true; h.has_battery = true; break;

        case 0x19: h.mbc = MbcType::Mbc5; break;
        case 0x1A: h.mbc = MbcType::Mbc5; h.has_ram = true; break;
        case 0x1B: h.mbc = MbcType::Mbc5; h.has_ram = true; h.has_battery = true; break;
        case 0x1C: h.mbc = MbcType::Mbc5; h.has_rumble = true; break;
        case 0x1D: h.mbc = MbcType::Mbc5; h.has_rumble = true; h.has_ram = true; break;
        case 0x1E: h.mbc = MbcType::Mbc5; h.has_rumble = true; h.has_ram = true; h.has_battery = true; break;

        case 0x20: h.mbc = MbcType::Mbc6; h.has_ram = true; h.has_battery = true; break;
        case 0x22: h.mbc = MbcType::Mbc7; h.has_rumble = true; h.has_ram = true; h.has_battery = true; break;

        case 0xFC: h.mbc = MbcType::PocketCamera; h.has_ram = true; h.has_battery = true; break;
        case 0xFD: h.mbc = MbcType::BandaiTama5; break;
        case 0xFE: h.mbc = MbcType::HuC3; break;
        case 0xFF: h.mbc = MbcType::HuC1; h.has_ram = true; h.has_battery = true; break;

        default: break;   // stays MbcType::Unknown
    }
}

}  // namespace

// ---------------------------------------------------------------------------
//  Names
// ---------------------------------------------------------------------------
const char *to_string(MbcType mbc)
{
    switch (mbc) {
        case MbcType::None:         return "none";
        case MbcType::Mbc1:         return "MBC1";
        case MbcType::Mbc2:         return "MBC2";
        case MbcType::Mbc3:         return "MBC3";
        case MbcType::Mbc5:         return "MBC5";
        case MbcType::Mbc6:         return "MBC6";
        case MbcType::Mbc7:         return "MBC7";
        case MbcType::Mmm01:        return "MMM01";
        case MbcType::HuC1:         return "HuC1";
        case MbcType::HuC3:         return "HuC3";
        case MbcType::PocketCamera: return "Pocket Camera";
        case MbcType::BandaiTama5:  return "Bandai TAMA5";
        case MbcType::Unknown:      return "unknown";
    }
    return "unknown";
}

const char *to_string(CgbSupport cgb)
{
    switch (cgb) {
        case CgbSupport::None:     return "no (DMG only)";
        case CgbSupport::Enhanced: return "enhanced (runs on DMG too)";
        case CgbSupport::Only:     return "CGB only";
    }
    return "?";
}

const char *cartridge_type_name(u8 type_code)
{
    switch (type_code) {
        case 0x00: return "ROM ONLY";
        case 0x01: return "MBC1";
        case 0x02: return "MBC1+RAM";
        case 0x03: return "MBC1+RAM+BATTERY";
        case 0x05: return "MBC2";
        case 0x06: return "MBC2+BATTERY";
        case 0x08: return "ROM+RAM";
        case 0x09: return "ROM+RAM+BATTERY";
        case 0x0B: return "MMM01";
        case 0x0C: return "MMM01+RAM";
        case 0x0D: return "MMM01+RAM+BATTERY";
        case 0x0F: return "MBC3+TIMER+BATTERY";
        case 0x10: return "MBC3+TIMER+RAM+BATTERY";
        case 0x11: return "MBC3";
        case 0x12: return "MBC3+RAM";
        case 0x13: return "MBC3+RAM+BATTERY";
        case 0x19: return "MBC5";
        case 0x1A: return "MBC5+RAM";
        case 0x1B: return "MBC5+RAM+BATTERY";
        case 0x1C: return "MBC5+RUMBLE";
        case 0x1D: return "MBC5+RUMBLE+RAM";
        case 0x1E: return "MBC5+RUMBLE+RAM+BATTERY";
        case 0x20: return "MBC6";
        case 0x22: return "MBC7+SENSOR+RUMBLE+RAM+BATTERY";
        case 0xFC: return "POCKET CAMERA";
        case 0xFD: return "BANDAI TAMA5";
        case 0xFE: return "HuC3";
        case 0xFF: return "HuC1+RAM+BATTERY";
        default:   return "unknown";
    }
}

bool is_mandatory_mbc(MbcType mbc)
{
    // Subject V.5 (p.8): ROM only, MBC1, MBC2 and MBC5.
    return mbc == MbcType::None || mbc == MbcType::Mbc1 ||
           mbc == MbcType::Mbc2 || mbc == MbcType::Mbc5;
}

// ---------------------------------------------------------------------------
//  Checksums
// ---------------------------------------------------------------------------
u8 compute_header_checksum(const std::vector<u8> &rom)
{
    u8 sum = 0;
    for (std::size_t a = kTitleStart; a <= kRomVersion; ++a) {
        // The u8 type makes the wrap-around happen on its own, exactly as the
        // 8-bit adder inside the console does.
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    return sum;
}

u16 compute_global_checksum(const std::vector<u8> &rom)
{
    u32 sum = 0;
    for (std::size_t a = 0; a < rom.size(); ++a) {
        if (a == kGlobalChecksum || a == kGlobalChecksum + 1) continue;  // skip itself
        sum += rom[a];
    }
    return static_cast<u16>(sum & 0xFFFF);
}

// ---------------------------------------------------------------------------
//  Parsing
// ---------------------------------------------------------------------------
bool parse_header(const std::vector<u8> &rom, CartridgeHeader &out, std::string &error)
{
    out = CartridgeHeader{};

    if (rom.size() < kHeaderEnd) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "file is too small to hold a cartridge header: %zu bytes, need at least %zu",
                      rom.size(), kHeaderEnd);
        error = buf;
        return false;
    }

    // --- Colour model (0x0143) ---------------------------------------------
    // Read first: it decides how long the title field is.
    const u8 cgb_byte = rom[kCgbFlag];
    if (cgb_byte == 0xC0)      out.cgb = CgbSupport::Only;
    else if (cgb_byte == 0x80) out.cgb = CgbSupport::Enhanced;
    else                       out.cgb = CgbSupport::None;

    // --- Title (0x0134..) ---------------------------------------------------
    // On DMG cartridges the title fills 16 bytes up to 0x0143. On CGB ones
    // 0x0143 became the colour flag, so the title stops one byte earlier.
    const std::size_t title_len = (out.cgb == CgbSupport::None) ? 16 : 15;
    out.title = read_text(rom, kTitleStart, title_len);

    // Newer cartridges carve a 4-character manufacturer code out of the title.
    // Only report it when all four bytes look like real text.
    bool manufacturer_ok = out.cgb != CgbSupport::None;
    for (std::size_t i = 0; manufacturer_ok && i < 4; ++i) {
        if (!is_printable(rom[kManufacturerCode + i])) manufacturer_ok = false;
    }
    if (manufacturer_ok) out.manufacturer_code = read_text(rom, kManufacturerCode, 4);

    // --- Licensee ----------------------------------------------------------
    // An old code of 0x33 means "look at the two ASCII bytes at 0x0144".
    if (rom[kOldLicensee] == 0x33) {
        if (is_printable(rom[kNewLicensee]) && is_printable(rom[kNewLicensee + 1]))
            out.licensee_code = read_text(rom, kNewLicensee, 2);
    } else {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02X", rom[kOldLicensee]);
        out.licensee_code = buf;
    }

    out.sgb = (rom[kSgbFlag] == 0x03);

    // --- Cartridge type (0x0147) -------------------------------------------
    out.type_code = rom[kCartridgeType];
    decode_type(out.type_code, out);
    if (out.mbc == MbcType::Unknown)
        out.warnings.push_back("unknown cartridge type code");

    // --- ROM size (0x0148) -------------------------------------------------
    // Encoded as a shift: real size = 32 KiB << code, i.e. 2 << code banks.
    out.rom_size_code = rom[kRomSize];
    if (out.rom_size_code <= 0x08) {
        out.rom_size  = static_cast<std::size_t>(32 * 1024) << out.rom_size_code;
        out.rom_banks = 2u << out.rom_size_code;
    } else {
        out.warnings.push_back("unknown ROM size code, falling back to the real file size");
        out.rom_size  = rom.size();
        out.rom_banks = static_cast<unsigned>((rom.size() + kBankSizeRom - 1) / kBankSizeRom);
    }
    if (out.rom_size != rom.size()) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "header announces %zu bytes of ROM but the file holds %zu",
                      out.rom_size, rom.size());
        out.warnings.push_back(buf);
    }

    // --- RAM size (0x0149) -------------------------------------------------
    // Not a shift this time, just a lookup table. Note that 0x05 (64 KiB) is
    // SMALLER than 0x04 (128 KiB): the codes are not ordered.
    out.ram_size_code = rom[kRamSize];
    switch (out.ram_size_code) {
        case 0x00: out.ram_size = 0;          break;
        case 0x01: out.ram_size = 0;          // listed but never used on real carts
                   out.warnings.push_back("RAM size code 0x01 is unused on real cartridges");
                   break;
        case 0x02: out.ram_size =   8 * 1024; break;
        case 0x03: out.ram_size =  32 * 1024; break;
        case 0x04: out.ram_size = 128 * 1024; break;
        case 0x05: out.ram_size =  64 * 1024; break;
        default:   out.ram_size = 0;
                   out.warnings.push_back("unknown RAM size code");
                   break;
    }
    out.ram_banks = static_cast<unsigned>(out.ram_size / kBankSizeRam);

    // MBC2 is the exception: its 512 x 4 bits live inside the controller and
    // are never announced at 0x0149.
    if (out.mbc == MbcType::Mbc2 && out.ram_size == 0) out.ram_size = 512;

    out.destination_code = rom[kDestination];
    out.rom_version      = rom[kRomVersion];

    // --- Checksums ----------------------------------------------------------
    out.header_checksum          = rom[kHeaderChecksum];
    out.computed_header_checksum = compute_header_checksum(rom);
    out.global_checksum          = static_cast<u16>((rom[kGlobalChecksum] << 8) |
                                                     rom[kGlobalChecksum + 1]);  // big-endian!
    out.computed_global_checksum = compute_global_checksum(rom);

    if (!out.header_checksum_valid())
        out.warnings.push_back("header checksum mismatch (real hardware would refuse to boot)");

    if (rom.size() % kBankSizeRom != 0)
        out.warnings.push_back("file size is not a whole number of 16 KiB banks");

    (void)kHeaderStart;
    (void)kLogoStart;
    return true;
}

// ---------------------------------------------------------------------------
//  Loading
// ---------------------------------------------------------------------------
bool Cartridge::load_from_file(const std::string &path, std::string &error)
{
    path_ = path;
    mbc_.reset();
    header_ = CartridgeHeader{};
    save_note_.clear();

    save_note_.clear();

    // A directory opens like a file and reads like nothing, so it has to be
    // turned away by name rather than discovered halfway through loading.
    struct stat info;
    if (stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
        error = "'" + path + "' is a directory, not a cartridge";
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "cannot open '" + path + "'";
        return false;
    }

    const std::streamoff size = file.tellg();
    if (size <= 0) {
        error = "'" + path + "' is empty";
        return false;
    }
    if (size > kMaxRomSize) {
        error = "'" + path + "' is too large to be a cartridge: " +
                std::to_string(static_cast<unsigned long long>(size)) +
                " bytes, and no cartridge exceeds 8 MiB";
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<u8> rom(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char *>(rom.data()), size)) {
        error = "cannot read '" + path + "'";
        return false;
    }

    if (!parse_header(rom, header_, error)) return false;

    mbc_           = make_mbc(header_, std::move(rom));
    bank_commands_ = 0;

    // A battery save, if there is one, is restored straight away: the whole
    // point of a battery is that the game finds its data where it left it.
    load_battery();
    return true;
}

bool Cartridge::load_from_memory(std::vector<u8> rom, const std::string &name, std::string &error)
{
    path_   = name;
    mbc_.reset();
    header_ = CartridgeHeader{};

    if (!parse_header(rom, header_, error)) return false;

    mbc_           = make_mbc(header_, std::move(rom));
    bank_commands_ = 0;
    return true;
}

// ---------------------------------------------------------------------------
//  Battery-backed RAM (subject V.5, p.8)
// ---------------------------------------------------------------------------
//  "You must also manage the in-game backup for games that propose this
//   feature (battery-backed cartridge RAM persisted on disk between
//   sessions)."
// ---------------------------------------------------------------------------
std::string Cartridge::save_path() const
{
    const std::size_t slash = path_.find_last_of('/');
    const std::size_t dot   = path_.find_last_of('.');

    // Only a dot in the final component is an extension, so a directory named
    // "my.roms" does not truncate the path.
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return path_.substr(0, dot) + ".sav";
    return path_ + ".sav";
}

bool Cartridge::load_battery()
{
    if (!has_battery() || !mbc_) return false;

    std::ifstream file(save_path(), std::ios::binary | std::ios::ate);
    if (!file) return false;   // no save yet is not an error

    std::vector<u8> &ram = mbc_->ram();
    const std::streamoff on_disk = file.tellg();

    // A mismatch is loaded anyway — a short file fills what it can and a long
    // one is cut — because refusing it would throw away a player's progress.
    // It is reported instead, since it almost always means the .sav belongs to
    // another cartridge.
    if (on_disk < 0 || static_cast<std::size_t>(on_disk) != ram.size()) {
        save_note_ = "save file is " +
                     std::to_string(on_disk < 0 ? 0LL : static_cast<long long>(on_disk)) +
                     " bytes, this cartridge has " + std::to_string(ram.size()) +
                     " bytes of RAM";
    }

    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(ram.data()), static_cast<std::streamsize>(ram.size()));
    return true;
}

bool Cartridge::save_battery() const
{
    if (!has_battery() || !mbc_) return false;

    // Nothing written means nothing to save, so a cartridge that was merely
    // looked at leaves no file behind.
    if (!mbc_->ram_written()) return false;

    std::ofstream file(save_path(), std::ios::binary | std::ios::trunc);
    if (!file) return false;

    const std::vector<u8> &ram = mbc_->ram();
    file.write(reinterpret_cast<const char *>(ram.data()),
               static_cast<std::streamsize>(ram.size()));
    return file.good();
}

const std::vector<u8> &Cartridge::ram() const
{
    static const std::vector<u8> kEmpty;
    return mbc_ ? mbc_->ram() : kEmpty;
}

// ---------------------------------------------------------------------------
//  Access from the bus
// ---------------------------------------------------------------------------
u8 Cartridge::read(u16 addr) const
{
    if (!mbc_) return 0xFF;
    if (addr < 0x8000) return mbc_->read_rom(addr);
    if (addr >= 0xA000 && addr < 0xC000) return mbc_->read_ram(addr);
    return 0xFF;
}

void Cartridge::write(u16 addr, u8 value)
{
    if (!mbc_) return;

    if (addr < 0x8000) {
        // Nothing is stored: ROM is read-only. The bytes reach the controller
        // inside the cartridge instead, which reads them as commands.
        ++bank_commands_;
        mbc_->write_rom(addr, value);
        return;
    }
    if (addr >= 0xA000 && addr < 0xC000) mbc_->write_ram(addr, value);
}

}  // namespace retroemu
