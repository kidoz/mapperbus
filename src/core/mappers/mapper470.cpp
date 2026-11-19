#include "core/mappers/mapper470.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x1000000UZ;
constexpr std::size_t kPrgBankSize = 0x008000UZ;
constexpr std::size_t kOuterBankSize = 0x040000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4700U;
constexpr Byte kInnerBankMask = 0x07U;
constexpr Byte kMirrorMask = 0x10U;
constexpr Byte kInnerMask = kInnerBankMask | kMirrorMask;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 470 oldid 20148.
Mapper470::Mapper470(const INesHeader& /*header*/,
                     std::vector<Byte> prg_rom,
                     const std::vector<Byte>& /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper470::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 470U) {
        return std::unexpected("Mapper 470 requires mapper ID 470");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 470 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 470 requires power-of-two 32 KiB-16 MiB PRG ROM and no CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 470 has no CPU work RAM or battery-backed RAM");
    }
    if (header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 470 requires exactly 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 470 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 470 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper470::prg_offset(Address addr) const {
    const std::size_t outer = static_cast<std::size_t>(registers_.outer) * kOuterBankSize;
    const std::size_t inner = (registers_.inner & kInnerBankMask) * kPrgBankSize;
    return outer + inner + (addr & (kPrgBankSize - 1U));
}

Byte Mapper470::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper470::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        registers_.inner = value & kInnerMask;
    }
}

bool Mapper470::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper470::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper470::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper470::write_chr(Address addr, Byte value) {
    chr_ram_[addr & (kChrRamSize - 1U)] = value;
}

MirrorMode Mapper470::mirror_mode() const {
    return (registers_.inner & kMirrorMask) != 0U ? MirrorMode::SingleUpper
                                                  : MirrorMode::SingleLower;
}

void Mapper470::write_expansion(Address addr, Byte value) {
    if (addr >= 0x5000U && addr < 0x6000U) {
        registers_.outer = value;
    }
}

void Mapper470::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper470::reset() {
    registers_ = {};
}

void Mapper470::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.outer);
    writer.write(registers_.inner);
    writer.write_array(chr_ram_);
}

void Mapper470::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next{};
    reader.read(next.outer);
    reader.read(next.inner);
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid = reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
                       chr_size == 0U && (next.inner & ~static_cast<unsigned>(kInnerMask)) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    registers_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
