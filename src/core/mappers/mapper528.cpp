#include "core/mappers/mapper528.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x060000UZ;
constexpr std::size_t kChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kGamePrgBanks = 0x10UZ;
constexpr std::size_t kGameChrBanks = 0x80UZ;
constexpr std::uint16_t kStateTag = 0x5280U;
constexpr std::uint16_t kIrqPrescalerReset = 341U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 528 oldid 8126.
// Power-up values are unspecified; reset selects game zero and deterministic banks.
Mapper528::Mapper528(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : initial_mirror_(header.mirror_mode), prg_rom_(std::move(prg_rom)),
      chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> Mapper528::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 528U) {
        return std::unexpected("Mapper 528 requires mapper ID 528");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 528 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != kChrSize) {
        return std::unexpected("Mapper 528 requires 384 KiB PRG and 256 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0x2000U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 528 requires exactly 8 KiB volatile PRG RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 528 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 528 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper528::prg_offset(Byte bank, Address addr) const {
    const std::size_t mask = state_.game == 0U ? 0x0FU : 0x1FU;
    const std::size_t base = state_.game == 0U ? 0U : kGamePrgBanks;
    const std::size_t physical_bank = base + (bank & mask);
    return physical_bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper528::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (state_.low_prg_bank == 1U) {
            return prg_ram_[addr - 0x6000U];
        }
        return prg_rom_[prg_offset(state_.low_prg_bank, addr)];
    }
    if (addr < 0x8000U) {
        return 0U;
    }

    const auto slot = (addr - 0x8000U) >> 13U;
    const std::size_t mask = state_.game == 0U ? 0x0FU : 0x1FU;
    Byte bank = static_cast<Byte>(mask);
    if (slot == 0U) {
        bank = state_.prg_banks[0U];
    } else if (slot == 1U) {
        bank = state_.prg_banks[1U];
    } else if (slot == 2U) {
        bank = static_cast<Byte>(mask - 1U);
    }
    return prg_rom_[prg_offset(bank, addr)];
}

void Mapper528::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (state_.low_prg_bank == 1U) {
            prg_ram_[addr - 0x6000U] = value;
        }
        return;
    }
    const auto group = static_cast<unsigned>(addr & 0xF000U);
    if (addr < 0x8000U || (group != 0xA000U && group != 0xC000U)) {
        return;
    }

    state_.game = (addr & 0x4000U) != 0U ? 1U : 0U;
    const auto reg = static_cast<unsigned>(addr & 0x0FU);
    if (reg <= 7U) {
        state_.chr_banks[reg] = value;
        return;
    }
    switch (reg) {
    case 0x08U:
        state_.low_prg_bank = value;
        break;
    case 0x09U:
    case 0x0AU:
        state_.prg_banks[reg - 0x09U] = value;
        break;
    case 0x0CU:
        state_.mirror = value & 3U;
        break;
    case 0x0DU:
        state_.irq_control = value & 7U;
        state_.irq_enabled = (value >> 1U) & 1U;
        state_.irq_pending = 0U;
        state_.irq_prescaler = kIrqPrescalerReset;
        if (state_.irq_enabled != 0U) {
            state_.irq_counter = state_.irq_latch;
        }
        break;
    case 0x0EU:
        state_.irq_pending = 0U;
        state_.irq_enabled = state_.irq_control & 1U;
        break;
    case 0x0FU:
        state_.irq_latch = value;
        break;
    default:
        break;
    }
}

bool Mapper528::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper528::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0x00U;
}

Byte Mapper528::read_chr(Address addr) {
    if (addr >= 0x2000U) {
        return 0U;
    }
    const std::size_t base = state_.game == 0U ? 0U : kGameChrBanks;
    const std::size_t bank = base + (state_.chr_banks[(addr >> 10U) & 7U] & 0x7FU);
    const std::size_t offset = bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset];
}

void Mapper528::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper528::mirror_mode() const {
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    return kModes[state_.mirror];
}

bool Mapper528::irq_pending() const {
    return state_.irq_pending != 0U;
}

void Mapper528::acknowledge_irq() {
    state_.irq_pending = 0U;
}

void Mapper528::tick_irq() {
    if (state_.irq_counter == 0xFFU) {
        state_.irq_counter = state_.irq_latch;
        state_.irq_pending = 1U;
    } else {
        ++state_.irq_counter;
    }
}

void Mapper528::clock_cpu() {
    if (state_.irq_enabled == 0U) {
        return;
    }
    if ((state_.irq_control & 4U) != 0U) {
        tick_irq();
    } else if (state_.irq_prescaler <= 3U) {
        state_.irq_prescaler += kIrqPrescalerReset - 3U;
        tick_irq();
    } else {
        state_.irq_prescaler -= 3U;
    }
}

void Mapper528::reset() {
    state_ = State{};
    state_.mirror = initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
}

void Mapper528::power_on() {
    reset();
    std::ranges::fill(prg_ram_, 0U);
}

void Mapper528::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(static_cast<Byte>(initial_mirror_));
    writer.write(state_.game);
    writer.write_array(state_.chr_banks);
    writer.write_array(state_.prg_banks);
    writer.write(state_.low_prg_bank);
    writer.write(state_.mirror);
    writer.write(state_.irq_latch);
    writer.write(state_.irq_counter);
    writer.write(state_.irq_prescaler);
    writer.write(state_.irq_control);
    writer.write(state_.irq_enabled);
    writer.write(state_.irq_pending);
    writer.write_array(prg_ram_);
}

void Mapper528::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    State next;
    next.game = reader.read<Byte>();
    reader.read_array(next.chr_banks);
    reader.read_array(next.prg_banks);
    next.low_prg_bank = reader.read<Byte>();
    next.mirror = reader.read<Byte>();
    next.irq_latch = reader.read<Byte>();
    next.irq_counter = reader.read<Byte>();
    next.irq_prescaler = reader.read<std::uint16_t>();
    next.irq_control = reader.read<Byte>();
    next.irq_enabled = reader.read<Byte>();
    next.irq_pending = reader.read<Byte>();
    std::array<Byte, 0x2000> prg_ram{};
    reader.read_array(prg_ram);

    const bool valid = reader.ok() && reader.remaining() == 0U && tag == kStateTag &&
                       prg_size == prg_rom_.size() && chr_size == chr_rom_.size() &&
                       initial_mirror == static_cast<Byte>(initial_mirror_) && next.game <= 1U &&
                       next.mirror <= 3U && next.irq_prescaler >= 1U &&
                       next.irq_prescaler <= kIrqPrescalerReset && next.irq_control <= 7U &&
                       next.irq_enabled <= 1U && next.irq_pending <= 1U &&
                       (next.irq_pending == 0U || next.irq_enabled != 0U);
    if (!valid) {
        reader.fail();
        return;
    }

    state_ = next;
    prg_ram_ = prg_ram;
}

} // namespace mapperbus::core
