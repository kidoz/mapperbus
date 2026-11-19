#include "core/mappers/mapper520.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x5200U;
constexpr Byte kPrgBankMask = 0x1FU;
constexpr Byte kChrBankMask = 0x0FU;
constexpr Byte kChrRamBankMask = 0x07U;
constexpr Byte kOuterPrgBankMask = 0x08U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 520 oldid 8107. Reset
// values are unspecified: use deterministic VRC4 defaults, retain volatile
// CHR-RAM on soft reset, and clear it only on power-on.
Mapper520::Mapper520(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper520::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 520U) {
        return std::unexpected("Mapper 520 requires mapper ID 520");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 520 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 520 requires power-of-two 32-512 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 520 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 520 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 520 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper520::outer_prg_bank() const {
    return (state_.chr_banks[state_.ppu_chr_slot] & kOuterPrgBankMask) >> 3U;
}

Byte Mapper520::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    Byte inner = 31U;
    if (slot == 1U) {
        inner = state_.prg_banks[1U];
    } else if (slot != 3U) {
        const bool swap = (state_.mode & 2U) != 0U;
        inner = (slot == 0U) != swap ? state_.prg_banks[0U] : 30U;
    }
    const auto bank = static_cast<std::size_t>((inner & kPrgBankMask) | (outer_prg_bank() << 5U));
    const auto offset = bank * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper520::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    const auto group = static_cast<unsigned>(addr) >> 12U;
    const auto port = static_cast<unsigned>((addr >> 2U) & 3U);
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
            bank = value & kChrBankMask;
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
            irq_control(value);
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

bool Mapper520::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper520::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper520::read_chr(Address addr) {
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    const auto bank = static_cast<std::size_t>(state_.chr_banks[slot] & kChrRamBankMask);
    return chr_ram_[bank * kChrBankSize + (addr & 0x03FFU)];
}

void Mapper520::write_chr(Address addr, Byte value) {
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    const auto bank = static_cast<std::size_t>(state_.chr_banks[slot] & kChrRamBankMask);
    chr_ram_[bank * kChrBankSize + (addr & 0x03FFU)] = value;
}

MirrorMode Mapper520::mirror_mode() const {
    return state_.mirror;
}

bool Mapper520::observes_ppu_address() const {
    return true;
}

void Mapper520::observe_ppu_address(Address addr) {
    if (addr < 0x2000U) {
        state_.ppu_chr_slot = static_cast<Byte>((addr >> 10U) & 7U);
    }
}

void Mapper520::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper520::reset() {
    state_ = State{};
    state_.mirror = initial_mirror_;
}

void Mapper520::irq_control(Byte value) {
    state_.irq_control = value & 7U;
    state_.irq_enabled = (value & 2U) != 0U ? 1U : 0U;
    state_.irq_pending = 0U;
    state_.irq_prescaler = 341U;
    if (state_.irq_enabled != 0U) {
        state_.irq_counter = state_.irq_latch;
    }
}

void Mapper520::irq_tick() {
    if (state_.irq_counter == 0xFFU) {
        state_.irq_counter = state_.irq_latch;
        state_.irq_pending = 1U;
    } else {
        ++state_.irq_counter;
    }
}

void Mapper520::clock_cpu() {
    if (state_.irq_enabled == 0U) {
        return;
    }
    if ((state_.irq_control & 4U) != 0U) {
        irq_tick();
    } else if (state_.irq_prescaler <= 3U) {
        state_.irq_prescaler += 338U;
        irq_tick();
    } else {
        state_.irq_prescaler -= 3U;
    }
}

bool Mapper520::irq_pending() const {
    return state_.irq_pending != 0U;
}

void Mapper520::acknowledge_irq() {
    state_.irq_pending = 0U;
}

std::uint16_t Mapper520::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper520::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write_array(state_.prg_banks);
    writer.write_array(state_.chr_banks);
    writer.write(state_.mirror);
    writer.write(state_.mode);
    writer.write(state_.ppu_chr_slot);
    writer.write(state_.irq_latch);
    writer.write(state_.irq_counter);
    writer.write(state_.irq_prescaler);
    writer.write(state_.irq_control);
    writer.write<Byte>((state_.irq_enabled != 0U ? 1U : 0U) | (state_.irq_pending != 0U ? 2U : 0U));
    writer.write_array(chr_ram_);
}

void Mapper520::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    State next;
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    reader.read(next.mirror);
    reader.read(next.mode);
    reader.read(next.ppu_chr_slot);
    reader.read(next.irq_latch);
    reader.read(next.irq_counter);
    reader.read(next.irq_prescaler);
    reader.read(next.irq_control);
    const auto irq_flags = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        std::ranges::any_of(next.prg_banks, [](Byte bank) { return bank > kPrgBankMask; }) ||
        std::ranges::any_of(next.chr_banks, [](Byte bank) { return bank > kChrBankMask; }) ||
        next.mirror == MirrorMode::FourScreen || next.mirror > MirrorMode::SingleUpper ||
        next.mode > 3U || next.ppu_chr_slot > 7U || next.irq_prescaler == 0U ||
        next.irq_prescaler > 341U || next.irq_control > 7U || irq_flags > 3U ||
        ((irq_flags & 2U) != 0U && (irq_flags & 1U) == 0U)) {
        reader.fail();
        return;
    }

    next.irq_enabled = irq_flags & 1U;
    next.irq_pending = (irq_flags & 2U) >> 1U;
    state_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
