#include "core/mappers/mapper487.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kRomSize = 0x180000UZ;
constexpr std::size_t kPrgWindowSize = 0x008000UZ;
constexpr std::size_t kChrWindowSize = 0x002000UZ;
constexpr std::size_t kChipPairBase = 0x080000UZ;
constexpr Address kNinaRegister = 0x4100U;
constexpr Address kOuterRegister = 0x4180U;
constexpr std::uint16_t kStateTag = 0x4870U;
constexpr Byte kMirrorMask = 0x80U;
constexpr Byte kLargeBankMask = 0x40U;
constexpr Byte kChipPairMask = 0x20U;
constexpr Byte kNinaInnerMask = 0x0FU;
constexpr Byte kColorDreamsInnerMask = 0x71U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 487 oldid 23361.
// Reset clears banking as a deterministic policy and restores header CIRAM.
Mapper487::Mapper487(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper487::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 487U) {
        return std::unexpected("Mapper 487 requires mapper ID 487");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 487 requires NES 2.0 submapper 0");
    }
    if (prg_size != kRomSize || chr_size != kRomSize) {
        return std::unexpected("Mapper 487 requires exactly 1536 KiB PRG and CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 487 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 487 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 487 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper487::outer_base() const {
    const bool chip_pair = (outer_bank_ & kChipPairMask) != 0U;
    const auto outer = static_cast<std::size_t>((outer_bank_ >> 1U) & (chip_pair ? 0x0FU : 0x07U));
    return (chip_pair ? kChipPairBase : 0UZ) + outer * 0x010000UZ;
}

std::size_t Mapper487::prg_offset(Address addr) const {
    const bool large_bank = (outer_bank_ & kLargeBankMask) != 0U;
    const bool chip_pair = (outer_bank_ & kChipPairMask) != 0U;
    const unsigned a15 = large_bank
                             ? (chip_pair ? static_cast<unsigned>(color_dreams_inner_bank_ & 1U)
                                          : static_cast<unsigned>((nina_inner_bank_ >> 3U) & 1U))
                             : static_cast<unsigned>(outer_bank_ & 1U);
    return outer_base() + static_cast<std::size_t>(a15) * kPrgWindowSize +
           (addr & (kPrgWindowSize - 1U));
}

std::size_t Mapper487::chr_offset(Address addr) const {
    const bool large_bank = (outer_bank_ & kLargeBankMask) != 0U;
    const bool chip_pair = (outer_bank_ & kChipPairMask) != 0U;
    const Byte inner = chip_pair ? color_dreams_inner_bank_ : nina_inner_bank_;
    const unsigned a15 =
        large_bank ? static_cast<unsigned>(chip_pair ? ((inner >> 6U) & 1U) : ((inner >> 2U) & 1U))
                   : static_cast<unsigned>(outer_bank_ & 1U);
    const unsigned a14_13 =
        chip_pair ? static_cast<unsigned>((inner >> 4U) & 3U) : static_cast<unsigned>(inner & 3U);
    return outer_base() + static_cast<std::size_t>(a15) * kPrgWindowSize +
           static_cast<std::size_t>(a14_13) * kChrWindowSize + (addr & (kChrWindowSize - 1U));
}

Byte Mapper487::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto offset = prg_offset(addr);
    return offset < prg_rom_.size() ? prg_rom_[offset] : 0U;
}

void Mapper487::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U && (outer_bank_ & kChipPairMask) != 0U) {
        color_dreams_inner_bank_ = value & kColorDreamsInnerMask;
    }
}

bool Mapper487::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper487::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper487::read_chr(Address addr) {
    if (addr >= kChrWindowSize) {
        return 0U;
    }
    const auto offset = chr_offset(addr);
    return offset < chr_rom_.size() ? chr_rom_[offset] : 0U;
}

void Mapper487::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper487::mirror_mode() const {
    return (outer_bank_ & kMirrorMask) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper487::write_expansion(Address addr, Byte value) {
    if (addr == kOuterRegister) {
        outer_bank_ = value;
    } else if (addr == kNinaRegister && (outer_bank_ & kChipPairMask) == 0U) {
        nina_inner_bank_ = value & kNinaInnerMask;
    }
}

void Mapper487::reset() {
    outer_bank_ = initial_mirror_ == MirrorMode::Horizontal ? kMirrorMask : 0U;
    nina_inner_bank_ = 0U;
    color_dreams_inner_bank_ = 0U;
}

std::uint16_t Mapper487::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper487::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(outer_bank_);
    writer.write(nina_inner_bank_);
    writer.write(color_dreams_inner_bank_);
}

void Mapper487::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer_bank = reader.read<Byte>();
    const auto nina_inner_bank = reader.read<Byte>();
    const auto color_dreams_inner_bank = reader.read<Byte>();

    const bool valid =
        reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
        chr_size == chr_rom_.size() &&
        (nina_inner_bank & ~static_cast<unsigned>(kNinaInnerMask)) == 0U &&
        (color_dreams_inner_bank & ~static_cast<unsigned>(kColorDreamsInnerMask)) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    outer_bank_ = outer_bank;
    nina_inner_bank_ = nina_inner_bank;
    color_dreams_inner_bank_ = color_dreams_inner_bank;
}

} // namespace mapperbus::core
