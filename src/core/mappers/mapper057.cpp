#include "core/mappers/mapper057.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPrgSize = 131072U;
constexpr std::size_t kChrSize = 131072U;
constexpr Address kRegisterMask = 0x8800U;
constexpr Byte kChrControlMask = 0xC3U;
constexpr std::uint16_t kStateTag = 0x3900U;
} // namespace

// Original decoding from NESdev r4333. The recorded 128/128 KiB population and
// consecutive X/X+1 pages in 32 KiB mode follow the Everynes hardware text.
// Zeroed latches on reset are a deterministic policy, not an electrical claim.
Mapper057::Mapper057(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper057::validate_geometry(const INesHeader& h,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 057 supports submapper 0");
    if (prg_size != kPrgSize || chr_size != kChrSize)
        return std::unexpected("Mapper 057 requires 128 KiB PRG ROM and 128 KiB CHR ROM");
    if (h.has_battery || (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U ||
                                        h.chr_ram_size != 0U || h.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 057 has no RAM, NVRAM or battery storage");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 057 supports ordinary H/V CIRAM metadata");
    return {};
}

Byte Mapper057::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    unsigned bank = (prg_control_ >> 5U) & 7U;
    if ((prg_control_ & 0x10U) != 0U && addr >= 0xC000U)
        bank = (bank + 1U) & 7U;
    const auto offset = static_cast<std::size_t>(bank) * 0x4000U + (addr & 0x3FFFU);
    return prg_rom_[offset];
}

void Mapper057::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    switch (addr & kRegisterMask) {
    case 0x8000U:
        chr_control_ = value & kChrControlMask;
        break;
    case 0x8800U:
        prg_control_ = value;
        break;
    default:
        break;
    }
}

Byte Mapper057::read_chr(Address addr) {
    const unsigned low = (chr_control_ & 0x40U) != 0U ? prg_control_ & 3U : chr_control_ & 3U;
    const unsigned bank = ((chr_control_ & 0x80U) >> 4U) | (prg_control_ & 4U) | low;
    const auto offset = static_cast<std::size_t>(bank) * 0x2000U + (addr & 0x1FFFU);
    return chr_rom_[offset];
}

void Mapper057::write_chr(Address, Byte) {}

MirrorMode Mapper057::mirror_mode() const {
    return (prg_control_ & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper057::reset() {
    chr_control_ = 0U;
    prg_control_ = 0U;
}

void Mapper057::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(chr_control_);
    writer.write(prg_control_);
}

void Mapper057::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto chr_control = reader.read<Byte>();
    const auto prg_control = reader.read<Byte>();
    if (!reader.ok() || tag != kStateTag || (chr_control & ~kChrControlMask) != 0U) {
        reader.fail();
        return;
    }
    chr_control_ = chr_control;
    prg_control_ = prg_control;
}

} // namespace mapperbus::core
