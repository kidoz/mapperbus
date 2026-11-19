#include "core/mappers/mapper290.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgUnitBytes = 16384U;
constexpr std::size_t kPrgBankBytes = 32768U;
constexpr std::size_t kChrBankBytes = 8192U;
constexpr std::size_t kMaxPrgBytes = 16U * kPrgBankBytes;
constexpr std::size_t kMaxChrBytes = 32U * kChrBankBytes;
constexpr Address kLatchMask = 0x7FC7U;
constexpr uint16_t kStateTag = 0x2900U;

} // namespace

// Original implementation from NESdev Mapper 290, immutable revision 7743.
Mapper290::Mapper290(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper290::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 290U || header.submapper != 0U) {
        return std::unexpected("Mapper 290 requires NES 2.0 mapper 290 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kPrgBankBytes || prg_size > kMaxPrgBytes ||
        !std::has_single_bit(chr_size) || chr_size < kChrBankBytes || chr_size > kMaxChrBytes) {
        return std::unexpected(
            "Mapper 290 requires power-of-two 32-512 KiB PRG and 8-256 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 290 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 290 requires H/V nametable wiring");
    }
    return {};
}

Byte Mapper290::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const std::size_t outer = (latch_ >> 11U) & 0x0FU;
    std::size_t offset = 0U;
    if ((latch_ & 0x0080U) != 0U) {
        const std::size_t bank = outer * 2U + ((latch_ >> 6U) & 1U);
        offset = bank * kPrgUnitBytes + (addr & 0x3FFFU);
    } else {
        offset = outer * kPrgBankBytes + (addr & 0x7FFFU);
    }
    return prg_rom_[offset & (prg_rom_.size() - 1U)];
}

void Mapper290::write_prg(Address addr, Byte) {
    if (addr >= 0x8000U) {
        latch_ = addr & kLatchMask;
    }
}

Byte Mapper290::read_chr(Address addr) {
    const std::size_t bank = ((latch_ >> 5U) & 0x18U) | (latch_ & 0x07U);
    const std::size_t offset = bank * kChrBankBytes + (addr & 0x1FFFU);
    return chr_rom_[offset & (chr_rom_.size() - 1U)];
}

void Mapper290::write_chr(Address, Byte) {}

MirrorMode Mapper290::mirror_mode() const {
    return (latch_ & 0x0400U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper290::reset() {
    // Deterministic console-reset policy; revision 7743 does not specify reset circuitry.
    latch_ = 0U;
}

void Mapper290::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(static_cast<uint32_t>(chr_rom_.size()));
    writer.write(latch_);
}

void Mapper290::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    const auto latch = reader.read<uint16_t>();
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }
    latch_ = latch;
}

} // namespace mapperbus::core
