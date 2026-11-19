#include "core/mappers/mapper463.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4630U;
constexpr Byte kModeMask = 0x07U;
constexpr Byte kPrgBaseMask = 0x7FU;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 463 oldid 22670 and its
// mapper-176 NROM subset. Zeroed reset latches are a deterministic policy.
Mapper463::Mapper463(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper463::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 463U) {
        return std::unexpected("Mapper 463 requires mapper ID 463");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 463 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxRomSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxRomSize) {
        return std::unexpected(
            "Mapper 463 requires power-of-two 32 KiB-2 MiB PRG and 8 KiB-2 MiB CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 463 has no RAM or nonvolatile storage");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 463 requires ordinary fixed H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 463 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper463::prg_offset(Address addr) const {
    const unsigned base = registers_.prg_base;
    if ((registers_.mode & 0x04U) != 0U) {
        return static_cast<std::size_t>(base & ~1U) * kPrgBankSize + (addr & 0x7FFFU);
    }
    return static_cast<std::size_t>(base) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper463::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper463::write_prg(Address, Byte) {}

bool Mapper463::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper463::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper463::chr_offset(Address addr) const {
    return static_cast<std::size_t>(registers_.chr_base) * kChrBankSize +
           (addr & (kChrBankSize - 1U));
}

Byte Mapper463::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper463::write_chr(Address, Byte) {}

MirrorMode Mapper463::mirror_mode() const {
    return initial_mirror_;
}

void Mapper463::write_expansion(Address addr, Byte value) {
    if (addr < 0x5000U || addr >= 0x6000U) {
        return;
    }
    switch (addr & 3U) {
    case 0U:
        registers_.mode = value & kModeMask;
        break;
    case 1U:
        registers_.prg_base = value & kPrgBaseMask;
        break;
    case 2U:
        registers_.chr_base = value;
        break;
    default:
        break;
    }
}

void Mapper463::power_on() {
    reset();
}

void Mapper463::reset() {
    registers_ = {};
}

std::uint16_t Mapper463::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U));
}

void Mapper463::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.mode);
    writer.write(registers_.prg_base);
    writer.write(registers_.chr_base);
}

void Mapper463::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next{};
    reader.read(next.mode);
    reader.read(next.prg_base);
    reader.read(next.chr_base);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || next.mode > kModeMask || next.prg_base > kPrgBaseMask) {
        reader.fail();
        return;
    }
    registers_ = next;
}

} // namespace mapperbus::core
