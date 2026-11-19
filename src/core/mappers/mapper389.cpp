#include "core/mappers/mapper389.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kNromBankSize = 0x8000UZ;
constexpr std::size_t kUnromBankSize = 0x4000UZ;
constexpr std::size_t kUnromOuterSize = 0x10000UZ;
constexpr std::size_t kChrBankSize = 0x2000UZ;
constexpr std::size_t kMaxPrgSize = 16UZ * kNromBankSize;
constexpr std::size_t kMaxChrSize = 32UZ * kChrBankSize;
constexpr std::uint16_t kStateTag = 0x3890U;
constexpr Byte kOuterPrgMask = 0x79U;
constexpr Byte kOuterChrMask = 0x3EU;
constexpr Byte kInnerBankMask = 0x0FU;
constexpr Byte kUnromMode = 0x02U;

} // namespace

// Original implementation from NESdev mapper 389 oldid 8004. The article gives
// zero power-on latch values; applying them on soft reset is deterministic policy.
Mapper389::Mapper389(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper389::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 389U) {
        return std::unexpected("Mapper 389 requires mapper ID 389");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 389 requires NES 2.0 submapper 0");
    }
    if (prg_size < kNromBankSize || prg_size > kMaxPrgSize || prg_size % kNromBankSize != 0U ||
        chr_size < kChrBankSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 389 requires 32-512 KiB PRG and 8-256 KiB CHR ROM in bank units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 389 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 389 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper389::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    std::size_t offset = 0U;
    if ((outer_chr_ & kUnromMode) == 0U) {
        const auto outer = static_cast<std::size_t>((outer_prg_ >> 3U) & 0x0FU);
        offset = outer * kNromBankSize + (addr & 0x7FFFU);
    } else {
        const auto outer = static_cast<std::size_t>((outer_prg_ >> 4U) & 0x07U);
        const Byte inner = addr < 0xC000U ? static_cast<Byte>((inner_bank_ >> 2U) & 0x03U) : 3U;
        offset = outer * kUnromOuterSize + static_cast<std::size_t>(inner) * kUnromBankSize +
                 (addr & 0x3FFFU);
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper389::write_prg(Address addr, Byte) {
    if ((addr & 0xF000U) == 0x8000U) {
        outer_prg_ = static_cast<Byte>(addr) & kOuterPrgMask;
    } else if ((addr & 0xF000U) == 0x9000U) {
        outer_chr_ = static_cast<Byte>(addr) & kOuterChrMask;
    } else if (addr >= 0xA000U) {
        inner_bank_ = static_cast<Byte>(addr) & kInnerBankMask;
    }
}

bool Mapper389::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper389::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper389::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto outer = static_cast<std::size_t>((outer_chr_ >> 2U) & 0x07U);
    const auto inner = static_cast<std::size_t>(inner_bank_ & 0x03U);
    const auto offset = outer * 4UZ * kChrBankSize + inner * kChrBankSize + (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper389::write_chr(Address, Byte) {}

MirrorMode Mapper389::mirror_mode() const {
    return (outer_prg_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper389::reset() {
    outer_prg_ = 0U;
    outer_chr_ = 0U;
    inner_bank_ = 0U;
}

void Mapper389::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(outer_prg_);
    writer.write(outer_chr_);
    writer.write(inner_bank_);
}

void Mapper389::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer_prg = reader.read<Byte>();
    const auto outer_chr = reader.read<Byte>();
    const auto inner_bank = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (outer_prg & ~kOuterPrgMask) != 0U ||
        (outer_chr & ~kOuterChrMask) != 0U || (inner_bank & ~kInnerBankMask) != 0U) {
        reader.fail();
        return;
    }

    outer_prg_ = outer_prg;
    outer_chr_ = outer_chr;
    inner_bank_ = inner_bank;
}

} // namespace mapperbus::core
