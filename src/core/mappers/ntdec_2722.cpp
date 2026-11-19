#include "core/mappers/ntdec_2722.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPrgBankSize = 8192U;
constexpr std::size_t kNromBankSize = 16384U;
constexpr std::uint16_t kIrqCounterMask = 0x1FFFU;
constexpr std::uint16_t kIrqPendingBit = 0x1000U;
constexpr std::uint16_t kStateTagBase = 0x2800U;
} // namespace

Ntdec2722::Ntdec2722(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), multicart_(header.is_nes2 && header.submapper == 1U) {
    reset();
}

Result<void> Ntdec2722::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper > 1U)
        return std::unexpected("Mapper 040 supports submappers 0 and 1");
    const bool multicart = header.is_nes2 && header.submapper == 1U;
    const bool valid_prg =
        multicart ? (prg_size == 65536U || prg_size == 131072U) : prg_size == 65536U;
    const bool valid_chr = multicart
                               ? (chr_size == 8192U || chr_size == 16384U || chr_size == 32768U)
                               : chr_size == 8192U;
    if (!valid_prg || !valid_chr)
        return std::unexpected(multicart
                                   ? "Mapper 040:1 requires 64/128 KiB PRG and 8/16/32 KiB CHR ROM"
                                   : "Mapper 040:0 requires 64 KiB PRG and 8 KiB CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 040 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 040 requires H/V startup mirroring");
    return {};
}

bool Ntdec2722::nrom_mode() const {
    return multicart_ && (outer_bank_ & 0x08U) != 0U;
}

Byte Ntdec2722::read_prg(Address addr) {
    if (addr < 0x6000U || (addr < 0x8000U && nrom_mode()))
        return 0U;

    if (nrom_mode()) {
        unsigned bank = 8U + ((outer_bank_ >> 5U) & 3U);
        if ((outer_bank_ & 0x04U) != 0U)
            bank = 8U + ((outer_bank_ >> 5U) & 2U) + ((addr >> 14U) & 1U);
        const std::size_t offset = bank * kNromBankSize + (addr & 0x3FFFU);
        return prg_rom_[offset % prg_rom_.size()];
    }

    unsigned bank = 7U;
    if (addr < 0x8000U)
        bank = 6U;
    else if (addr < 0xA000U)
        bank = 4U;
    else if (addr < 0xC000U)
        bank = 5U;
    else if (addr < 0xE000U)
        bank = prg_bank_;
    const std::size_t offset = bank * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Ntdec2722::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    switch (addr & 0xE000U) {
    case 0x8000U:
        irq_enabled_ = false;
        irq_counter_ = 0U;
        break;
    case 0xA000U:
        irq_enabled_ = true;
        break;
    case 0xC000U:
        if (multicart_)
            outer_bank_ = value & 0x7FU;
        break;
    case 0xE000U:
        prg_bank_ = value & 7U;
        break;
    default:
        break;
    }
}

bool Ntdec2722::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && !nrom_mode());
}

Byte Ntdec2722::read_chr(Address addr) {
    const unsigned bank = multicart_ ? ((outer_bank_ >> 1U) & 3U) : 0U;
    const std::size_t offset = bank * kPrgBankSize + (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Ntdec2722::write_chr(Address, Byte) {}

MirrorMode Ntdec2722::mirror_mode() const {
    if (!multicart_)
        return initial_mirror_;
    return (outer_bank_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Ntdec2722::reset() {
    // Electrical power-up latches are unspecified; use deterministic reset defaults.
    prg_bank_ = 0U;
    outer_bank_ = multicart_ && initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
    irq_counter_ = 0U;
    irq_enabled_ = false;
}

void Ntdec2722::clock_cpu() {
    if (irq_enabled_)
        irq_counter_ = (irq_counter_ + 1U) & kIrqCounterMask;
}

bool Ntdec2722::irq_pending() const {
    return irq_enabled_ && (irq_counter_ & kIrqPendingBit) != 0U;
}

std::uint16_t Ntdec2722::state_tag() const {
    return static_cast<std::uint16_t>(kStateTagBase | (multicart_ ? 0x10U : 0U) |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Ntdec2722::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(prg_bank_);
    writer.write(outer_bank_);
    writer.write(irq_counter_);
    writer.write(static_cast<Byte>(irq_enabled_));
}

void Ntdec2722::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto outer_bank = reader.read<Byte>();
    const auto irq_counter = reader.read<std::uint16_t>();
    const auto irq_enabled = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag() || prg_bank > 7U || outer_bank > 0x7FU ||
        (!multicart_ && outer_bank != 0U) || irq_counter > kIrqCounterMask || irq_enabled > 1U ||
        (irq_enabled == 0U && irq_counter != 0U)) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    outer_bank_ = outer_bank;
    irq_counter_ = irq_counter;
    irq_enabled_ = irq_enabled != 0U;
}

} // namespace mapperbus::core
