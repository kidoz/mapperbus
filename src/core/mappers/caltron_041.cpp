#include "core/mappers/caltron_041.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Byte kOuterBankMask = 0x3FU;
constexpr Byte kInnerChrBankMask = 0x03U;
constexpr std::uint16_t kStateTag = 0x2900U;
} // namespace

Caltron041::Caltron041(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {}

Result<void> Caltron041::validate_geometry(const INesHeader& header,
                                           std::size_t prg_size,
                                           std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 041 supports submapper 0");
    if (prg_size != 0x40000U || chr_size != 0x20000U)
        return std::unexpected("Mapper 041 requires 256 KiB PRG and 128 KiB CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 041 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 041 requires H/V CIRAM metadata");
    return {};
}

Byte Caltron041::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto bank = static_cast<std::size_t>(outer_bank_ & 3U);
    return prg_rom_[bank * 0x8000U + (addr & 0x7FFFU)];
}

void Caltron041::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x6800U) {
        outer_bank_ = value & kOuterBankMask;
        return;
    }
    if (addr >= 0x8000U && (outer_bank_ & 4U) != 0U)
        inner_chr_bank_ = value & read_prg(addr) & kInnerChrBankMask;
}

Byte Caltron041::read_chr(Address addr) {
    const auto outer = static_cast<std::size_t>((outer_bank_ >> 3U) & 3U);
    return chr_rom_[outer * 0x8000U + static_cast<std::size_t>(inner_chr_bank_) * 0x2000U +
                    (addr & 0x1FFFU)];
}

void Caltron041::write_chr(Address, Byte) {}

MirrorMode Caltron041::mirror_mode() const {
    return (outer_bank_ & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Caltron041::reset() {
    outer_bank_ = 0U;
    inner_chr_bank_ = 0U;
}

void Caltron041::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(outer_bank_);
    writer.write(inner_chr_bank_);
}

void Caltron041::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto inner = reader.read<Byte>();
    if (!reader.ok() || tag != kStateTag || outer > kOuterBankMask || inner > kInnerChrBankMask) {
        reader.fail();
        return;
    }
    outer_bank_ = outer;
    inner_chr_bank_ = inner;
}

} // namespace mapperbus::core
