#include "core/mappers/mapper271.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Byte kLatchMask = 0x3FU;
constexpr uint16_t kStateTag = 0x010FU;
} // namespace

// Original decoding from NESdev mapper 271 r24533. Electrical reset behavior is
// unspecified; clearing the latch is the deterministic compatibility policy.
Bmc22026::Bmc22026(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {}

Result<void> Bmc22026::validate_geometry(const INesHeader& header,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 271 supports submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x20000U || prg_size % 0x8000U != 0U ||
        chr_size < 0x2000U || chr_size > 0x20000U || chr_size % 0x2000U != 0U)
        return std::unexpected("Mapper 271 requires 32-128 KiB PRG ROM and 8-128 KiB CHR ROM");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 271 requires ROM-only memory with no battery");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 271 supports ordinary H/V CIRAM");
    return {};
}

Byte Bmc22026::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const std::size_t bank = (latch_ >> 4U) & 3U;
    const std::size_t offset = bank * 0x8000U + (addr & 0x7FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Bmc22026::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        latch_ = static_cast<Byte>(value & read_prg(addr) & kLatchMask);
}

Byte Bmc22026::read_chr(Address addr) {
    const std::size_t offset = (latch_ & 0x0FU) * 0x2000U + (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Bmc22026::write_chr(Address, Byte) {}

MirrorMode Bmc22026::mirror_mode() const {
    return (latch_ & 0x10U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

void Bmc22026::reset() {
    latch_ = 0U;
}

void Bmc22026::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(latch_);
}

void Bmc22026::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto latch = reader.read<Byte>();
    if (!reader.ok() || tag != kStateTag || (latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }
    latch_ = latch;
}

} // namespace mapperbus::core
