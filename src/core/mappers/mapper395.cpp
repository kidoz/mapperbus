#include "core/mappers/mapper395.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x200000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x3950U;
constexpr Byte kOuter1Mask = 0x38U;
constexpr Byte kOuter2Mask = 0xF9U;
constexpr Byte kOuterLock = 0x80U;

bool outer1_register(Address addr) {
    return (addr & 0xE010U) == 0x6000U;
}

} // namespace

// Original implementation from NESdev mapper 395 oldid 8017. Reset behavior is
// unspecified; clearing registers on reset is a deterministic compatibility policy.
Mapper395::Mapper395(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper395::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 395U) {
        return std::unexpected("Mapper 395 requires mapper ID 395");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 395 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 395 requires 32 KiB-2 MiB PRG ROM in 8 KiB units");
    }
    if (chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected("Mapper 395 requires 8 KiB-2 MiB CHR ROM in 1 KiB units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 395 requires ROM-only memory metadata");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 395 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected(
            "Mapper 395 does not support trainer writes through outer registers");
    }
    return {};
}

unsigned Mapper395::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (registers_.bank_mode & 0x40U) != 0U;
    const bool narrow = (registers_.outer2 & 0x08U) != 0U;
    const unsigned mask = narrow ? 0x0FU : 0x1FU;
    if (slot == 1U) {
        return registers_.banks[7U] & mask;
    }
    if ((slot == 0U) != swapped) {
        return registers_.banks[6U] & mask;
    }
    if (slot == 3U) {
        return mask;
    }
    return mask - 1U;
}

Byte Mapper395::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return 0U;
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const unsigned inner = inner_prg_bank(addr);
    const unsigned a17 =
        (registers_.outer2 & 0x08U) != 0U ? registers_.outer2 & 1U : (inner >> 4U) & 1U;
    const unsigned bank = (inner & 0x0FU) | (a17 << 4U) | (((registers_.outer1 >> 4U) & 1U) << 5U) |
                          (((registers_.outer1 >> 5U) & 1U) << 6U) |
                          (((registers_.outer1 >> 3U) & 1U) << 7U);
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper395::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((registers_.ram_flags & 0xC0U) != 0x80U) {
            return;
        }
        if ((registers_.outer2 & kOuterLock) == 0U) {
            if (outer1_register(addr)) {
                registers_.outer1 = value & kOuter1Mask;
            } else {
                registers_.outer2 = value & kOuter2Mask;
            }
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.bank_select = value & 7U;
        registers_.bank_mode = value & 0xC0U;
        break;
    case 0x8001U:
        registers_.banks[registers_.bank_select] =
            registers_.bank_select >= 6U ? value & 0x3FU : value;
        break;
    case 0xA000U:
        registers_.mirror = (value & 1U) != 0U ? 0U : 1U;
        break;
    case 0xA001U:
        registers_.ram_flags = value & 0xC0U;
        break;
    case 0xC000U:
        registers_.irq_reload = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_flags |= 1U;
        break;
    case 0xE000U:
        registers_.irq_flags &= 1U;
        break;
    case 0xE001U:
        registers_.irq_flags |= 2U;
        break;
    default:
        break;
    }
}

bool Mapper395::maps_prg(Address addr) const {
    if (addr >= 0x8000U) {
        return true;
    }
    return false;
}

Byte Mapper395::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper395::inner_chr_bank(Address addr) const {
    unsigned slot = (static_cast<unsigned>(addr) >> 10U) & 7U;
    if ((registers_.bank_mode & 0x80U) != 0U) {
        slot ^= 4U;
    }
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

Byte Mapper395::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const unsigned inner = inner_chr_bank(addr);
    const unsigned a17 =
        (registers_.outer2 & 0x40U) != 0U ? ((registers_.outer2 >> 4U) & 1U) : (inner >> 7U) & 1U;
    const unsigned bank = (inner & 0x7FU) | (a17 << 7U) | (((registers_.outer1 >> 4U) & 1U) << 8U) |
                          (((registers_.outer1 >> 5U) & 1U) << 9U) |
                          (((registers_.outer2 >> 5U) & 1U) << 10U);
    const auto offset = static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper395::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper395::mirror_mode() const {
    return static_cast<MirrorMode>(registers_.mirror);
}

void Mapper395::power_on() {
    reset();
}

void Mapper395::reset() {
    registers_ = Registers{};
    registers_.mirror = static_cast<Byte>(initial_mirror_);
}

bool Mapper395::irq_pending() const {
    return (registers_.irq_flags & 4U) != 0U;
}

void Mapper395::acknowledge_irq() {
    registers_.irq_flags &= 3U;
}

void Mapper395::clock_irq_counter() {
    if (registers_.irq_counter == 0U || (registers_.irq_flags & 1U) != 0U) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_flags &= 6U;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & 2U) != 0U) {
        registers_.irq_flags |= 4U;
    }
}

std::uint16_t Mapper395::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper395::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer1);
    writer.write(registers_.outer2);
    writer.write(registers_.bank_select);
    writer.write(registers_.bank_mode);
    writer.write_array(registers_.banks);
    writer.write(registers_.mirror);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write(registers_.irq_flags);
    writer.write(registers_.ram_flags);
}

void Mapper395::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer1);
    reader.read(next.outer2);
    reader.read(next.bank_select);
    reader.read(next.bank_mode);
    reader.read_array(next.banks);
    reader.read(next.mirror);
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    reader.read(next.irq_flags);
    reader.read(next.ram_flags);

    const bool valid_registers =
        (next.outer1 & ~kOuter1Mask) == 0U && (next.outer2 & ~kOuter2Mask) == 0U &&
        next.bank_select <= 7U && (next.bank_mode & 0x3FU) == 0U && next.banks[6] <= 0x3FU &&
        next.banks[7] <= 0x3FU && next.mirror <= 1U && next.irq_flags <= 7U &&
        ((next.irq_flags & 4U) == 0U || (next.irq_flags & 2U) != 0U) &&
        (next.ram_flags & 0x3FU) == 0U;
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || !valid_registers) {
        reader.fail();
        return;
    }

    registers_ = next;
}

} // namespace mapperbus::core
