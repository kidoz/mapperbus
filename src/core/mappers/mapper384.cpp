#include "core/mappers/mapper384.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kWramSize = 0x0800UZ;
constexpr std::uint16_t kStateTag = 0x3840U;
constexpr Byte kOuterLock = 0x08U;
constexpr Byte kOuterBankMask = 0x03U;

} // namespace

// Original implementation from NESdev mapper 384 oldid 7998. Soft reset uses
// the documented zero outer-bank power-on value and deterministic VRC4 defaults.
Mapper384::Mapper384(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper384::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 384U) {
        return std::unexpected("Mapper 384 requires mapper ID 384");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 384 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || !std::has_single_bit(prg_size) ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || !std::has_single_bit(chr_size)) {
        return std::unexpected(
            "Mapper 384 requires power-of-two 32-512 KiB PRG and 8-512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != kWramSize || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 384 requires only 2 KiB volatile PRG RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 384 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

bool Mapper384::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x7000U && (mode_ & 1U) != 0U);
}

Byte Mapper384::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper384::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return maps_prg(addr) ? wram_[addr & 0x07FFU] : 0U;
    }
    if (prg_rom_.empty()) {
        return 0U;
    }

    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    Byte inner = 15U;
    if (slot == 1U) {
        inner = prg_banks_[1U];
    } else if (slot != 3U) {
        const bool swap = (mode_ & 2U) != 0U;
        inner = (slot == 0U) != swap ? prg_banks_[0U] : 14U;
    }
    const auto bank =
        static_cast<std::size_t>((inner & 0x0FU) | ((outer_bank_ & kOuterBankMask) << 4U));
    const auto offset = bank * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper384::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (addr >= 0x6800U && addr < 0x7000U && (outer_bank_ & kOuterLock) == 0U) {
            outer_bank_ = value & 0x0FU;
        }
        if (addr >= 0x6000U && addr < 0x7000U && (mode_ & 1U) != 0U) {
            wram_[addr & 0x07FFU] = value;
        }
        return;
    }
    write_vrc4(addr, value);
}

void Mapper384::write_vrc4(Address addr, Byte value) {
    const auto group = static_cast<unsigned>(addr) >> 12U;
    const auto port = static_cast<unsigned>((addr >> 2U) & 3U);
    if (group == 8U || group == 10U) {
        prg_banks_[group == 8U ? 0U : 1U] = value & 0x1FU;
    } else if (group == 9U) {
        if (port == 0U) {
            constexpr std::array kModes = {MirrorMode::Vertical,
                                           MirrorMode::Horizontal,
                                           MirrorMode::SingleLower,
                                           MirrorMode::SingleUpper};
            mirror_mode_ = kModes[value & 3U];
        } else if (port == 2U) {
            mode_ = value & 3U;
        }
    } else if (group >= 11U && group <= 14U) {
        auto& bank = chr_banks_[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U) {
            bank = (bank & 0x01F0U) | (value & 0x0FU);
        } else {
            bank = (bank & 0x000FU) | ((value & 0x1FU) << 4U);
        }
    } else if (group == 15U) {
        switch (port) {
        case 0U:
            irq_latch_ = (irq_latch_ & 0xF0U) | (value & 0x0FU);
            break;
        case 1U:
            irq_latch_ = (irq_latch_ & 0x0FU) | ((value & 0x0FU) << 4U);
            break;
        case 2U:
            irq_control(value);
            break;
        case 3U:
            irq_pending_ = false;
            irq_enabled_ = (irq_control_ & 1U) != 0U;
            break;
        default:
            break;
        }
    }
}

Byte Mapper384::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto inner = chr_banks_[(addr >> 10U) & 7U];
    const auto bank =
        static_cast<std::size_t>((inner & 0x007FU) | ((outer_bank_ & kOuterBankMask) << 7U));
    const auto offset = bank * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper384::write_chr(Address, Byte) {}

MirrorMode Mapper384::mirror_mode() const {
    return mirror_mode_;
}

void Mapper384::power_on() {
    reset();
    wram_.fill(0U);
}

void Mapper384::reset() {
    outer_bank_ = 0U;
    prg_banks_ = {0U, 1U};
    chr_banks_.fill(0U);
    mirror_mode_ = initial_mirror_;
    mode_ = 0U;
    irq_latch_ = 0U;
    irq_counter_ = 0U;
    irq_prescaler_ = 341U;
    irq_control_ = 0U;
    irq_enabled_ = false;
    irq_pending_ = false;
}

void Mapper384::irq_control(Byte value) {
    irq_control_ = value & 7U;
    irq_enabled_ = (value & 2U) != 0U;
    irq_pending_ = false;
    irq_prescaler_ = 341U;
    if (irq_enabled_) {
        irq_counter_ = irq_latch_;
    }
}

void Mapper384::irq_tick() {
    if (irq_counter_ == 0xFFU) {
        irq_counter_ = irq_latch_;
        irq_pending_ = true;
    } else {
        ++irq_counter_;
    }
}

void Mapper384::clock_cpu() {
    if (!irq_enabled_) {
        return;
    }
    if ((irq_control_ & 4U) != 0U) {
        irq_tick();
    } else if (irq_prescaler_ <= 3U) {
        irq_prescaler_ += 338U;
        irq_tick();
    } else {
        irq_prescaler_ -= 3U;
    }
}

bool Mapper384::irq_pending() const {
    return irq_pending_;
}

void Mapper384::acknowledge_irq() {
    irq_pending_ = false;
}

std::uint16_t Mapper384::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper384::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(outer_bank_);
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(mirror_mode_);
    writer.write(mode_);
    writer.write(irq_latch_);
    writer.write(irq_counter_);
    writer.write(irq_prescaler_);
    writer.write(irq_control_);
    writer.write<Byte>((irq_enabled_ ? 1U : 0U) | (irq_pending_ ? 2U : 0U));
    writer.write_array(wram_);
}

void Mapper384::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer_bank = reader.read<Byte>();
    std::array<Byte, 2> prg_banks{};
    std::array<std::uint16_t, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto mirror = reader.read<MirrorMode>();
    const auto mode = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_prescaler = reader.read<std::uint16_t>();
    const auto irq_control = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();
    std::array<Byte, 0x0800> wram{};
    reader.read_array(wram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || outer_bank > 0x0FU ||
        std::ranges::any_of(prg_banks, [](Byte bank) { return bank > 0x1FU; }) ||
        std::ranges::any_of(chr_banks, [](std::uint16_t bank) { return bank > 0x01FFU; }) ||
        mirror == MirrorMode::FourScreen || mirror > MirrorMode::SingleUpper || mode > 3U ||
        irq_prescaler == 0U || irq_prescaler > 341U || irq_control > 7U || irq_flags > 3U ||
        ((irq_flags & 2U) != 0U && (irq_flags & 1U) == 0U)) {
        reader.fail();
        return;
    }

    outer_bank_ = outer_bank;
    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    mirror_mode_ = mirror;
    mode_ = mode;
    irq_latch_ = irq_latch;
    irq_counter_ = irq_counter;
    irq_prescaler_ = irq_prescaler;
    irq_control_ = irq_control;
    irq_enabled_ = (irq_flags & 1U) != 0U;
    irq_pending_ = (irq_flags & 2U) != 0U;
    wram_ = wram;
}

} // namespace mapperbus::core
