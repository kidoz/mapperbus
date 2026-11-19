#include "core/mappers/mapper324.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 1024U * 1024U;
constexpr std::size_t kOuterBankSize = 128U * 1024U;
constexpr std::size_t kInnerBankSize = 16U * 1024U;
constexpr std::size_t kChrRamSize = 8192U;
constexpr Byte kOuterLatchMask = 0x78U;
constexpr Byte kUnromLatchMask = 0x87U;
constexpr Byte kOuterLock = 0x08U;
constexpr Byte kClockGenerator = 0x80U;
constexpr std::uint16_t kStateTag = 0x3240U;

} // namespace

// Original implementation from NESdev mapper 324 r24547. The non-resettable C/III
// latch has no documented power-up value; zero initialization is a deterministic policy.
Mapper324::Mapper324(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper324::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 324U || !header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 324 requires NES 2.0 mapper 324 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 324 requires exactly 1 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 324 requires exactly 8 KiB volatile CHR RAM and no other cartridge RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 324 requires hard-wired H/V nametable metadata");
    }
    return {};
}

Byte Mapper324::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const std::size_t outer = (outer_latch_ >> 4U) & 7U;
    const std::size_t inner = addr < 0xC000U ? unrom_latch_ & 7U : 7U;
    const std::size_t offset = outer * kOuterBankSize + inner * kInnerBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset];
}

void Mapper324::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    const Byte latched = value & read_prg(addr);
    if ((unrom_latch_ & kClockGenerator) == 0U && (latched & kClockGenerator) != 0U &&
        (outer_latch_ & kOuterLock) == 0U) {
        outer_latch_ = latched & kOuterLatchMask;
    }
    unrom_latch_ = latched & kUnromLatchMask;
}

Byte Mapper324::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper324::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper324::mirror_mode() const {
    return mirror_;
}

void Mapper324::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper324::reset() {
    outer_latch_ = 0U;
}

std::uint16_t Mapper324::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | (mirror_ == MirrorMode::Horizontal ? 1U : 0U));
}

void Mapper324::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(outer_latch_);
    writer.write(unrom_latch_);
    writer.write_array(chr_ram_);
}

void Mapper324::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto outer_latch = reader.read<Byte>();
    const auto unrom_latch = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        (outer_latch & ~kOuterLatchMask) != 0U || (unrom_latch & ~kUnromLatchMask) != 0U) {
        reader.fail();
        return;
    }

    outer_latch_ = outer_latch;
    unrom_latch_ = unrom_latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
