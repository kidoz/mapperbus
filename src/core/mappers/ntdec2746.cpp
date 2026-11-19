#include "core/mappers/ntdec2746.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Address kLatchMask = 0x03FFU;
constexpr uint16_t kStateTag = 0x0105U;
} // namespace

// Original decoding from NESdev mapper 261 r18984. Reset is a deterministic
// latch-clearing policy; the article does not specify electrical reset inputs.
Ntdec2746::Ntdec2746(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {}

Result<void> Ntdec2746::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 261 supports submapper 0");
    if (prg_size < 0x4000U || prg_size > 0x40000U || prg_size % 0x4000U != 0U ||
        chr_size < 0x2000U || chr_size > 0x20000U || chr_size % 0x2000U != 0U)
        return std::unexpected("Mapper 261 requires 16-256 KiB PRG ROM and 8-128 KiB CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 0U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 261 requires ROM-only memory with no battery");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 261 supports ordinary H/V CIRAM");
    return {};
}

Byte Ntdec2746::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    std::size_t offset = 0U;
    if ((latch_ & 0x20U) != 0U) {
        const std::size_t bank = (latch_ >> 7U) & 7U;
        offset = bank * 0x8000U + (addr & 0x7FFFU);
    } else {
        const std::size_t bank = (latch_ >> 6U) & 0x0FU;
        offset = bank * 0x4000U + (addr & 0x3FFFU);
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Ntdec2746::write_prg(Address addr, Byte /*value*/) {
    if (addr >= 0x8000U)
        latch_ = addr & kLatchMask;
}

Byte Ntdec2746::read_chr(Address addr) {
    const std::size_t offset = (latch_ & 0x0FU) * 0x2000U + (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Ntdec2746::write_chr(Address, Byte) {}

MirrorMode Ntdec2746::mirror_mode() const {
    return (latch_ & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Ntdec2746::reset() {
    latch_ = 0U;
}

void Ntdec2746::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(latch_);
}

void Ntdec2746::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto latch = reader.read<Address>();
    if (!reader.ok() || tag != kStateTag || (latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }
    latch_ = latch;
}

} // namespace mapperbus::core
