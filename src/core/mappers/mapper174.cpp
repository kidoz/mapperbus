#include "core/mappers/mapper174.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPrgSize = 128U * 1024U;
constexpr std::size_t kChrSize = 64U * 1024U;
constexpr uint16_t kStateTag = 0xAE00U;
} // namespace

Ntdec174::Ntdec174(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {}

Result<void> Ntdec174::validate_geometry(const INesHeader& header,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 174 supports submapper 0");
    if (prg_size != kPrgSize || chr_size != kChrSize)
        return std::unexpected("Mapper 174 requires 128 KiB PRG and 64 KiB CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 0U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 174 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 174 supports ordinary H/V CIRAM");
    return {};
}

Byte Ntdec174::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    if ((latch_ & 0x80U) != 0U) {
        const auto bank = static_cast<std::size_t>((latch_ >> 5U) & 3U);
        return prg_rom_[bank * 0x8000U + (addr & 0x7FFFU)];
    }
    const auto bank = static_cast<std::size_t>((latch_ >> 4U) & 7U);
    return prg_rom_[bank * 0x4000U + (addr & 0x3FFFU)];
}

void Ntdec174::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        latch_ = value;
}

Byte Ntdec174::read_chr(Address addr) {
    const auto bank = static_cast<std::size_t>((latch_ >> 1U) & 7U);
    return chr_rom_[bank * 0x2000U + (addr & 0x1FFFU)];
}

void Ntdec174::write_chr(Address, Byte) {}

MirrorMode Ntdec174::mirror_mode() const {
    return (latch_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Ntdec174::reset() {
    // The documented counter-based latch has no specified console-reset input.
}

void Ntdec174::power_on() {
    latch_ = 0U;
}

void Ntdec174::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(latch_);
}

void Ntdec174::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto latch = reader.read<Byte>();
    if (!reader.ok() || tag != kStateTag) {
        reader.fail();
        return;
    }
    latch_ = latch;
}

} // namespace mapperbus::core
