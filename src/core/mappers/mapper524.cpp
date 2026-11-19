#include "core/mappers/mapper524.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x040000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x5240U;
constexpr std::uint16_t kIrqCounterMask = 0x07FFU;
constexpr std::uint16_t kIrqPeriodMask = 0x0400U;
constexpr Byte kPrgBankMask = 0x1FU;
constexpr Byte kChrNibbleMask = 0x0FU;

} // namespace

// Original implementation from NES 2.0 Mapper 524 oldid 8116 and its
// UNIF/900218 VRC2/VRC4e references. Reset values are unspecified: restore
// deterministic VRC defaults, the header CIRAM mode and a stopped IRQ.
Mapper524::Mapper524(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper524::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 524U) {
        return std::unexpected("Mapper 524 requires mapper ID 524");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 524 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 524 requires power-of-two 32-256 KiB PRG and 8-256 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 524 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 524 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 524 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper524::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x7000U) {
        return state_.microwire;
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const std::size_t count = prg_rom_.size() / kPrgBankSize;
    std::size_t bank = count - 1U;
    if (slot == 0U) {
        bank = state_.prg_banks[0U];
    } else if (slot == 1U) {
        bank = state_.prg_banks[1U];
    } else if (slot == 2U) {
        bank = count - 2U;
    }
    const std::size_t offset = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper524::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x7000U) {
        state_.microwire = value & 1U;
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    const auto group = static_cast<unsigned>(addr) >> 12U;
    const auto port = static_cast<unsigned>((addr >> 2U) & 3U);
    if (group == 8U || group == 10U) {
        state_.prg_banks[group == 8U ? 0U : 1U] = value & kPrgBankMask;
    } else if (group == 9U) {
        if (port == 0U) {
            state_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        }
    } else if (group >= 11U && group <= 14U) {
        auto& bank = state_.chr_banks[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U) {
            bank = (bank & 0xF0U) | (value & kChrNibbleMask);
        } else {
            bank = (bank & kChrNibbleMask) | ((value & kChrNibbleMask) << 4U);
        }
    } else if (group == 15U) {
        if (port == 2U) {
            state_.irq_enabled = 1U;
        } else if (port == 3U) {
            state_.irq_counter = 0U;
            state_.irq_enabled = 0U;
        }
    }
}

bool Mapper524::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x7000U);
}

Byte Mapper524::prg_read_mask(Address addr) const {
    if (addr >= 0x8000U) {
        return 0xFFU;
    }
    return addr >= 0x6000U && addr < 0x7000U ? 0x01U : 0x00U;
}

Byte Mapper524::read_chr(Address addr) {
    if (addr >= 0x2000U || chr_rom_.empty()) {
        return 0U;
    }
    const auto bank = static_cast<std::size_t>(state_.chr_banks[(addr >> 10U) & 7U]);
    const std::size_t offset = bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper524::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper524::mirror_mode() const {
    return state_.mirror;
}

void Mapper524::reset() {
    state_ = State{};
    state_.mirror = initial_mirror_;
}

void Mapper524::clock_cpu() {
    if (state_.irq_enabled != 0U) {
        state_.irq_counter = (state_.irq_counter + 1U) & kIrqCounterMask;
    }
}

bool Mapper524::irq_pending() const {
    return state_.irq_enabled != 0U && (state_.irq_counter & kIrqPeriodMask) != 0U;
}

void Mapper524::acknowledge_irq() {
    state_.irq_counter = 0U;
    state_.irq_enabled = 0U;
}

std::uint16_t Mapper524::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper524::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write_array(state_.prg_banks);
    writer.write_array(state_.chr_banks);
    writer.write<Byte>(state_.mirror == MirrorMode::Horizontal ? 1U : 0U);
    writer.write(state_.microwire);
    writer.write(state_.irq_counter);
    writer.write(state_.irq_enabled);
}

void Mapper524::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    State next;
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    const auto mirror = reader.read<Byte>();
    next.microwire = reader.read<Byte>();
    next.irq_counter = reader.read<std::uint16_t>();
    next.irq_enabled = reader.read<Byte>();

    if (!reader.ok() || reader.remaining() != 0U || tag != state_tag() ||
        prg_size != prg_rom_.size() || chr_size != chr_rom_.size() ||
        next.prg_banks[0U] > kPrgBankMask || next.prg_banks[1U] > kPrgBankMask || mirror > 1U ||
        next.microwire > 1U || next.irq_counter > kIrqCounterMask || next.irq_enabled > 1U) {
        reader.fail();
        return;
    }

    next.mirror = mirror == 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    state_ = next;
}

} // namespace mapperbus::core
