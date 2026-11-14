#include "core/mappers/maxi15.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kChipSize = 0x80000U;
} // namespace

// Original implementation from NESdev r24540, Mark's preliminary maxi15.txt
// hardware description and krzysiobal's three-chip board report (thread 24627).
// Prefer r24540 for the corrected locking/reset rules; see the coverage ledger.
Maxi15::Maxi15(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      fixed_chr_(prg_rom_.size() == 2U * kChipSize && chr_rom_.size() == kChipSize) {}

Result<void> Maxi15::validate_geometry(const INesHeader& h,
                                       std::size_t prg_size,
                                       std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 234 supports submapper 0");
    if (!((prg_size == kChipSize && chr_size == kChipSize) ||
          (prg_size == 2U * kChipSize && (chr_size == kChipSize || chr_size == 2U * kChipSize))))
        return std::unexpected("Mapper 234 requires 512/512 KiB, 1024/512 KiB or 1024/1024 KiB "
                               "PRG/CHR ROM populations");
    if (h.has_battery || h.chr_ram_size != 0U || h.chr_nvram_size != 0U ||
        (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 234 has no cartridge RAM or battery storage");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 234 supports ordinary H/V CIRAM");
    return {};
}

bool Maxi15::secondary_disabled(std::size_t rom_size) const {
    return (outer_ & 0x20U) != 0U && ((outer_ & 0x10U) != 0U || rom_size == kChipSize);
}

Byte Maxi15::prg_read_mask(Address addr) const {
    return addr >= 0x8000U && secondary_disabled(prg_rom_.size()) ? 0U : 0xFFU;
}

Byte Maxi15::read_prg(Address addr) {
    if (addr < 0x8000U || secondary_disabled(prg_rom_.size()))
        return 0U;
    const unsigned bank = (outer_ & 0x40U) != 0U ? (outer_ & 14U) | (inner_ & 1U) : outer_ & 15U;
    const std::size_t chip = (outer_ & 0x20U) != 0U ? kChipSize : 0U;
    return prg_rom_[chip + bank * 0x8000U + (addr & 0x7FFFU)];
}

void Maxi15::clock_register(Address addr, Byte value) {
    if (addr >= 0xFF80U && addr <= 0xFF9FU) {
        if ((outer_ & 0x3FU) == 0U)
            outer_ = value;
    } else if (addr >= 0xFFC0U && addr <= 0xFFDFU) {
        if ((outer_ & 0x3FU) == 0U)
            lockout_ = value & 3U;
    } else if (addr >= 0xFFE8U && addr <= 0xFFF7U) {
        inner_ = value & 0x71U;
    }
}

void Maxi15::observe_cpu_read(Address addr, Byte value) {
    // The completed read supplied its old-map byte, including opcode, dummy
    // and DMA reads. Direct mapper inspection deliberately does not clock it.
    clock_register(addr, value);
}

void Maxi15::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    if (!secondary_disabled(prg_rom_.size()))
        value &= read_prg(addr); // Sample the driven old-map ROM byte before updating latches.
    clock_register(addr, value);
}

Byte Maxi15::read_chr(Address addr) {
    if (!fixed_chr_ && secondary_disabled(chr_rom_.size()))
        // Consistent with undriven external nametable reads in the PPU: the
        // multiplexed bus retains the low address byte. This is emulator policy.
        return static_cast<Byte>(addr);
    const bool nina = (outer_ & 0x40U) != 0U;
    const unsigned bank = nina ? (outer_ & 14U) * 4U + ((inner_ >> 4U) & 7U)
                               : (outer_ & 15U) * 4U + ((inner_ >> 4U) & 3U);
    const std::size_t chip = !fixed_chr_ && (outer_ & 0x20U) != 0U ? kChipSize : 0U;
    return chr_rom_[chip + bank * 0x2000U + (addr & 0x1FFFU)];
}

void Maxi15::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Maxi15::mirror_mode() const {
    return (outer_ & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Maxi15::reset() {
    outer_ = 0U;
    inner_ = 0U;
    lockout_ = 0U;
}

uint16_t Maxi15::state_tag() const {
    return static_cast<uint16_t>(0xEA00U | (prg_rom_.size() > kChipSize ? 1U : 0U) |
                                 (chr_rom_.size() > kChipSize ? 2U : 0U));
}

void Maxi15::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(outer_);
    writer.write(inner_);
    writer.write(lockout_);
}

void Maxi15::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto inner = reader.read<Byte>();
    const auto lockout = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag() || (inner & ~0x71U) != 0U || lockout > 3U) {
        reader.fail();
        return;
    }
    outer_ = outer;
    inner_ = inner;
    lockout_ = lockout;
}

} // namespace mapperbus::core
