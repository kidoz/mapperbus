#include "core/mappers/mapper530.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x040000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kFixedSecondLastPrgBank = 0x1EUZ;
constexpr std::size_t kFixedLastPrgBank = 0x1FUZ;
constexpr std::uint16_t kStateTag = 0x5300U;
constexpr std::uint16_t kIrqPrescalerReset = 341U;
constexpr Byte kPrgBankMask = 0x1FU;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 530 oldid 8130.
// Reset values are unspecified: use deterministic VRC4 defaults. The supported
// geometry is ROM-only with no expansion audio; smaller fitted ROMs wrap only
// after the VRC4 bank decode and external address-pin exchanges.
Mapper530::Mapper530(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : initial_mirror_(header.mirror_mode), prg_rom_(std::move(prg_rom)),
      chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper530::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 530U) {
        return std::unexpected("Mapper 530 requires mapper ID 530");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 530 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 530 requires power-of-two 32-256 KiB PRG and 8-512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 530 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 530 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 530 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper530::scramble_prg_bank(std::size_t bank) {
    return (bank & ~0x05UZ) | ((bank & 0x01UZ) << 2U) | ((bank & 0x04UZ) >> 2U);
}

std::size_t Mapper530::scramble_chr_bank(std::size_t bank) {
    return (bank & ~0x60UZ) | ((bank & 0x20UZ) << 1U) | ((bank & 0x40UZ) >> 1U);
}

bool Mapper530::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper530::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper530::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_.empty()) {
        return 0U;
    }

    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const bool swap = (state_.mode & 2U) != 0U;
    std::size_t bank = kFixedLastPrgBank;
    if (slot == 0U) {
        bank = swap ? kFixedSecondLastPrgBank : state_.prg_banks[0U];
    } else if (slot == 1U) {
        bank = state_.prg_banks[1U];
    } else if (slot == 2U) {
        bank = swap ? state_.prg_banks[0U] : kFixedSecondLastPrgBank;
    }
    const std::size_t offset =
        scramble_prg_bank(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper530::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    const auto group = static_cast<unsigned>(addr >> 12U);
    const auto port = static_cast<unsigned>(addr) & 3U;
    if (group == 8U || group == 10U) {
        state_.prg_banks[group == 8U ? 0U : 1U] = value & kPrgBankMask;
    } else if (group == 9U) {
        if (port == 0U) {
            constexpr std::array kModes = {MirrorMode::Vertical,
                                           MirrorMode::Horizontal,
                                           MirrorMode::SingleLower,
                                           MirrorMode::SingleUpper};
            state_.mirror = kModes[value & 3U];
        } else if (port == 2U) {
            state_.mode = value & 3U;
        }
    } else if (group >= 11U && group <= 14U) {
        auto& bank = state_.chr_banks[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U) {
            bank = (bank & 0x1F0U) | (value & 0x0FU);
        } else {
            bank = (bank & 0x00FU) | ((value & 0x1FU) << 4U);
        }
    } else if (group == 15U) {
        switch (port) {
        case 0U:
            state_.irq_latch = (state_.irq_latch & 0xF0U) | (value & 0x0FU);
            break;
        case 1U:
            state_.irq_latch = (state_.irq_latch & 0x0FU) | ((value & 0x0FU) << 4U);
            break;
        case 2U:
            state_.irq_control = value & 7U;
            state_.irq_enabled = (value >> 1U) & 1U;
            state_.irq_pending = 0U;
            state_.irq_prescaler = kIrqPrescalerReset;
            if (state_.irq_enabled != 0U) {
                state_.irq_counter = state_.irq_latch;
            }
            break;
        case 3U:
            state_.irq_pending = 0U;
            state_.irq_enabled = state_.irq_control & 1U;
            break;
        default:
            break;
        }
    }
}

Byte Mapper530::read_chr(Address addr) {
    if (addr >= 0x2000U || chr_rom_.empty()) {
        return 0U;
    }
    const auto bank = state_.chr_banks[(addr >> 10U) & 7U];
    const std::size_t offset =
        scramble_chr_bank(bank) * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper530::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper530::mirror_mode() const {
    return state_.mirror;
}

bool Mapper530::irq_pending() const {
    return state_.irq_pending != 0U;
}

void Mapper530::acknowledge_irq() {
    state_.irq_pending = 0U;
}

void Mapper530::tick_irq() {
    if (state_.irq_counter == 0xFFU) {
        state_.irq_counter = state_.irq_latch;
        state_.irq_pending = 1U;
    } else {
        ++state_.irq_counter;
    }
}

void Mapper530::clock_cpu() {
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

void Mapper530::reset() {
    state_ = State{};
    state_.mirror = initial_mirror_;
}

void Mapper530::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(static_cast<Byte>(initial_mirror_));
    writer.write_array(state_.prg_banks);
    writer.write_array(state_.chr_banks);
    writer.write(state_.mirror);
    writer.write(state_.mode);
    writer.write(state_.irq_latch);
    writer.write(state_.irq_counter);
    writer.write(state_.irq_prescaler);
    writer.write(state_.irq_control);
    writer.write(state_.irq_enabled);
    writer.write(state_.irq_pending);
}

void Mapper530::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    State next;
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    reader.read(next.mirror);
    reader.read(next.mode);
    reader.read(next.irq_latch);
    reader.read(next.irq_counter);
    reader.read(next.irq_prescaler);
    reader.read(next.irq_control);
    reader.read(next.irq_enabled);
    reader.read(next.irq_pending);

    const bool valid =
        reader.ok() && reader.remaining() == 0U && tag == kStateTag &&
        prg_size == prg_rom_.size() && chr_size == chr_rom_.size() &&
        initial_mirror == static_cast<Byte>(initial_mirror_) &&
        std::ranges::all_of(next.prg_banks, [](Byte bank) { return bank <= kPrgBankMask; }) &&
        std::ranges::all_of(next.chr_banks, [](std::uint16_t bank) { return bank <= 0x01FFU; }) &&
        next.mirror != MirrorMode::FourScreen && next.mirror <= MirrorMode::SingleUpper &&
        next.mode <= 3U && next.irq_prescaler >= 1U && next.irq_prescaler <= kIrqPrescalerReset &&
        next.irq_control <= 7U && next.irq_enabled <= 1U && next.irq_pending <= 1U &&
        (next.irq_pending == 0U || next.irq_enabled != 0U);
    if (!valid) {
        reader.fail();
        return;
    }

    state_ = next;
}

} // namespace mapperbus::core
