#include "core/mappers/mapper526.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x5260U;
constexpr std::uint16_t kIrqLineMask = 0x1000U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 526 oldid 20369.
// The probable $800F decoder is used as published. Its unresolved IRQ roles use
// $800D to clear bit 12 and $800F to reset the counter. Counter width and reset
// values are unspecified: retain 16 bits and start with linear ROM banks.
Mapper526::Mapper526(const INesHeader& /*header*/,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper526::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 526U) {
        return std::unexpected("Mapper 526 requires mapper ID 526");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 526 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 526 requires power-of-two 32 KiB-2 MiB PRG and 8-256 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 526 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 526 requires hard-wired vertical CIRAM");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 526 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper526::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const std::size_t offset = static_cast<std::size_t>(state_.prg_banks[slot]) * kPrgBankSize +
                               (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper526::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    const auto reg = static_cast<unsigned>(addr & 0x000FU);
    if (reg < 8U) {
        state_.chr_banks[reg] = value;
    } else if (reg < 12U) {
        state_.prg_banks[reg - 8U] = value;
    } else if (reg == 13U) {
        acknowledge_irq();
    } else if (reg == 15U) {
        state_.irq_counter = 0U;
    }
}

bool Mapper526::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper526::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper526::read_chr(Address addr) {
    if (addr >= 0x2000U || chr_rom_.empty()) {
        return 0U;
    }
    const auto bank = static_cast<std::size_t>(state_.chr_banks[(addr >> 10U) & 7U]);
    const std::size_t offset = bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper526::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper526::mirror_mode() const {
    return MirrorMode::Vertical;
}

void Mapper526::reset() {
    state_ = State{};
}

void Mapper526::clock_cpu() {
    state_.irq_counter = static_cast<std::uint16_t>(state_.irq_counter + 1U);
}

bool Mapper526::irq_pending() const {
    return (state_.irq_counter & kIrqLineMask) != 0U;
}

void Mapper526::acknowledge_irq() {
    state_.irq_counter &= static_cast<std::uint16_t>(~kIrqLineMask);
}

void Mapper526::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write_array(state_.prg_banks);
    writer.write_array(state_.chr_banks);
    writer.write(state_.irq_counter);
}

void Mapper526::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    State next;
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    next.irq_counter = reader.read<std::uint16_t>();

    if (!reader.ok() || reader.remaining() != 0U || tag != kStateTag ||
        prg_size != prg_rom_.size() || chr_size != chr_rom_.size()) {
        reader.fail();
        return;
    }

    state_ = next;
}

} // namespace mapperbus::core
