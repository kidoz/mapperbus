#include "core/mappers/mapper403.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4030U;
constexpr Address kLowRegisterMask = 0xE103U;
constexpr Byte kPrgBankMask = 0x7EU;
constexpr Byte kChrBankMask = 0x03U;
constexpr Byte kModeMask = 0x15U;
constexpr Byte kHorizontalMirroring = 0x10U;
constexpr Byte kHighChrLatchEnable = 0x04U;
constexpr Byte kNrom128Mode = 0x01U;

bool maps_low_register(Address addr) {
    switch (addr & kLowRegisterMask) {
    case 0x4100U:
    case 0x4101U:
    case 0x4102U:
        return true;
    default:
        return false;
    }
}

} // namespace

// Original implementation from NESdev mapper 403 oldid 18960. Soft reset reapplies
// the documented power-on register values and retains CHR RAM; cold power clears it.
Mapper403::Mapper403(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper403::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 403U) {
        return std::unexpected("Mapper 403 requires mapper ID 403");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 403 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 403 requires 32 KiB-1 MiB PRG ROM in 16 KiB units");
    }
    if (chr_size != 0U) {
        return std::unexpected("Mapper 403 requires no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 403 requires only 32 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 403 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 403 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper403::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    unsigned bank = (registers_.prg_bank & kPrgBankMask) >> 1U;
    if ((registers_.mode & kNrom128Mode) == 0U) {
        bank = (bank & 0x3EU) | ((addr & 0x4000U) != 0U ? 1U : 0U);
    }
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper403::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U && (registers_.mode & kHighChrLatchEnable) != 0U) {
        registers_.chr_bank = value & kChrBankMask;
    }
}

bool Mapper403::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper403::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper403::chr_offset(Address addr) const {
    return static_cast<std::size_t>(registers_.chr_bank) * kChrBankSize + (addr & 0x1FFFU);
}

Byte Mapper403::read_chr(Address addr) {
    return chr_ram_[chr_offset(addr)];
}

void Mapper403::write_chr(Address addr, Byte value) {
    chr_ram_[chr_offset(addr)] = value;
}

MirrorMode Mapper403::mirror_mode() const {
    return (registers_.mode & kHorizontalMirroring) != 0U ? MirrorMode::Horizontal
                                                          : MirrorMode::Vertical;
}

bool Mapper403::maps_expansion(Address addr) const {
    return maps_low_register(addr);
}

Byte Mapper403::expansion_read_mask(Address addr) const {
    return maps_low_register(addr) ? 0U : 0xFFU;
}

void Mapper403::write_expansion(Address addr, Byte value) {
    switch (addr & kLowRegisterMask) {
    case 0x4100U:
        registers_.prg_bank = value & kPrgBankMask;
        break;
    case 0x4101U:
        registers_.chr_bank = value & kChrBankMask;
        break;
    case 0x4102U:
        registers_.mode = value & kModeMask;
        break;
    default:
        break;
    }
}

void Mapper403::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper403::reset() {
    registers_ = Registers{};
}

void Mapper403::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.prg_bank);
    writer.write(registers_.chr_bank);
    writer.write(registers_.mode);
    writer.write_array(chr_ram_);
}

void Mapper403::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.prg_bank);
    reader.read(next.chr_bank);
    reader.read(next.mode);
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        (next.prg_bank & ~kPrgBankMask) != 0U || (next.chr_bank & ~kChrBankMask) != 0U ||
        (next.mode & ~kModeMask) != 0U) {
        reader.fail();
        return;
    }

    registers_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
