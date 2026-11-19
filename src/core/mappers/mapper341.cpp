#include "core/mappers/mapper341.hpp"

#include <bit>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kChrBankSize = 0x2000UZ;
constexpr std::size_t kMaxPrgSize = 8UZ * kPrgBankSize;
constexpr std::size_t kMaxChrSize = 8UZ * kChrBankSize;
constexpr std::uint16_t kStateTag = 0x3410U;
constexpr Byte kBankMask = 0x07U;
constexpr Byte kHorizontalMask = 0x08U;
constexpr Byte kControlMask = kBankMask | kHorizontalMask;

} // namespace

// Board decoding follows NESdev mapper 341 oldid 7876. Reset is unspecified;
// selecting bank zero and vertical mirroring is a deterministic policy.
Mapper341::Mapper341(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper341::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 341U) {
        return std::unexpected("Mapper 341 requires mapper ID 341");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 341 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kPrgBankSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kChrBankSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 341 requires power-of-two 16-128 KiB PRG and 8-64 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 341 requires ROM-only memory with no battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 341 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper341::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const std::size_t offset = static_cast<std::size_t>(control_ & kBankMask) * kPrgBankSize +
                               (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper341::write_prg(Address addr, Byte) {
    if (addr >= 0x8000U) {
        control_ = static_cast<Byte>(((addr >> 8U) & kBankMask) |
                                     ((addr & 0x02U) != 0U ? kHorizontalMask : 0U));
    }
}

Byte Mapper341::read_chr(Address addr) {
    const std::size_t offset = static_cast<std::size_t>(control_ & kBankMask) * kChrBankSize +
                               (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper341::write_chr(Address, Byte) {}

MirrorMode Mapper341::mirror_mode() const {
    return (control_ & kHorizontalMask) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper341::reset() {
    control_ = 0U;
}

void Mapper341::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(control_);
}

void Mapper341::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto control = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (control & ~kControlMask) != 0U) {
        reader.fail();
        return;
    }

    control_ = control;
}

} // namespace mapperbus::core
