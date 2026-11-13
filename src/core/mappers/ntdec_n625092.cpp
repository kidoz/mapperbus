#include "core/mappers/ntdec_n625092.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Address kOuterMask = 0x031FU;
constexpr uint16_t kStateTag = 0xDD00U;
} // namespace

// Original decoding from NESdev mapper 221 r5744. The 32 KiB mode follows the
// article's bank-size/formula description despite its repeated "NROM-128" label.
NtdecN625092::NtdecN625092(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> NtdecN625092::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 221 supports submapper 0");
    if (prg_size < 0x4000U || prg_size > 0x200000U || prg_size % 0x4000U != 0U || chr_size != 0U)
        return std::unexpected("Mapper 221 requires 16 KiB-2 MiB PRG ROM and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 221 requires 8 KiB volatile CHR RAM and no CPU RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 221 supports ordinary H/V CIRAM");
    return {};
}

Byte NtdecN625092::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const unsigned block = ((outer_ >> 2U) & 7U) | ((outer_ >> 6U) & 8U);
    unsigned bank = inner_ & 7U;
    if ((outer_ & 2U) != 0U) {
        if ((outer_ & 0x100U) != 0U) {
            if (addr >= 0xC000U)
                bank = 7U;
        } else {
            bank = (bank & 6U) | ((addr >> 14U) & 1U);
        }
    }
    const auto offset = block * 0x20000U + bank * 0x4000U + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void NtdecN625092::write_prg(Address addr, Byte /*value*/) {
    if (addr >= 0xC000U)
        inner_ = static_cast<Byte>(addr & 15U);
    else if (addr >= 0x8000U)
        outer_ = addr & kOuterMask;
}

Byte NtdecN625092::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void NtdecN625092::write_chr(Address addr, Byte value) {
    if ((inner_ & 8U) == 0U)
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode NtdecN625092::mirror_mode() const {
    return (outer_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void NtdecN625092::reset() {
    // Deterministic reset-latch policy; volatile pattern memory is retained.
    outer_ = 0U;
    inner_ = 0U;
}

void NtdecN625092::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(outer_);
    writer.write(inner_);
    writer.write_array(chr_ram_);
}

void NtdecN625092::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto outer = reader.read<Address>();
    const auto inner = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != kStateTag || (outer & ~kOuterMask) != 0U || inner > 15U) {
        reset();
        reader.fail();
        return;
    }
    outer_ = outer;
    inner_ = inner;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
