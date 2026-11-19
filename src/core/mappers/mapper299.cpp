#include "core/mappers/mapper299.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kRomSize = 128U * 1024U;
constexpr Byte kLatchMask = 0xB3U;
constexpr std::uint16_t kStateTag = 0x12B0U;

} // namespace

// Original implementation from NESdev mapper 299 r22947. The pinned article
// specifies the complete latch and contains no linked hardware references.
Mapper299::Mapper299(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> Mapper299::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 299U)
        return std::unexpected("Mapper 299 requires mapper ID 299");
    if (!header.is_nes2 || header.submapper != 0U)
        return std::unexpected("Mapper 299 requires NES 2.0 submapper 0");
    if (prg_size != kRomSize || chr_size != kRomSize)
        return std::unexpected("Mapper 299 requires 128 KiB PRG and 128 KiB CHR ROM");
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 299 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 299 requires H/V nametable metadata");
    return {};
}

Byte Mapper299::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const std::size_t outer = (latch_ >> 4U) & 3U;
    return prg_rom_[outer * 0x8000U + (addr & 0x7FFFU)];
}

void Mapper299::write_prg(Address addr, Byte value) {
    if ((addr & 0x8000U) != 0U)
        latch_ = value & kLatchMask;
}

Byte Mapper299::read_chr(Address addr) {
    const std::size_t outer = (latch_ >> 4U) & 3U;
    const std::size_t inner = latch_ & 3U;
    return chr_rom_[outer * 0x8000U + inner * 0x2000U + (addr & 0x1FFFU)];
}

void Mapper299::write_chr(Address, Byte) {}

MirrorMode Mapper299::mirror_mode() const {
    return (latch_ & 0x80U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

void Mapper299::power_on() {
    // Electrical power-up is undocumented; zero is the deterministic cold-start policy.
    latch_ = 0U;
}

void Mapper299::reset() {
    // No console-reset input is documented, so soft reset has no effect.
}

void Mapper299::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(latch_);
}

void Mapper299::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Byte>();
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (latch & static_cast<Byte>(~kLatchMask)) != 0U) {
        reader.fail();
        return;
    }
    latch_ = latch;
}

} // namespace mapperbus::core
