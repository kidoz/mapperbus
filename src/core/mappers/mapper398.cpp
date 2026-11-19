#include "core/mappers/mapper398.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x040000UZ;
constexpr std::size_t kChrSize = 0x0A0000UZ;
constexpr std::size_t kOuterPrgSize = 0x020000UZ;
constexpr std::size_t kSecondChrChip = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x3980U;
constexpr std::uint16_t kIrqPrescalerReset = 341U;
constexpr Byte kOuterMask = 0xC0U;
constexpr Byte kOuterMode = 0x80U;
constexpr Byte kOuterA = 0x40U;

} // namespace

// Original implementation from NESdev mapper 398 oldid 8021. Soft reset reapplies
// the documented outer power-on value and deterministic VRC defaults; the article
// does not specify a separate electrical soft-reset sequence.
Mapper398::Mapper398(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper398::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 398U) {
        return std::unexpected("Mapper 398 requires mapper ID 398");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 398 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != kChrSize) {
        return std::unexpected("Mapper 398 requires exactly 256 KiB PRG and 640 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 398 requires ROM-only memory with no battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 398 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 398 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper398::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    std::size_t offset = 0U;
    if ((registers_.outer & kOuterMode) != 0U) {
        offset = kOuterPrgSize + ((registers_.outer & kOuterA) != 0U ? 0x010000UZ : 0UZ) +
                 (registers_.prg_chr_a12 != 0U ? 0x008000UZ : 0UZ) + (addr & 0x7FFFU);
    } else {
        const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
        const bool swapped = (registers_.prg_control & 2U) != 0U;
        unsigned bank = 15U;
        if (slot == 1U) {
            bank = registers_.prg_banks[1U] & 0x0FU;
        } else if (slot == 0U || slot == 2U) {
            bank = (slot == 0U) != swapped ? registers_.prg_banks[0U] & 0x0FU : 14U;
        }
        offset = ((registers_.outer & kOuterA) != 0U ? kOuterPrgSize : 0UZ) + bank * kPrgBankSize +
                 (addr & (kPrgBankSize - 1U));
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper398::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    registers_.outer = value & kOuterMask;

    const Address reg = addr & 0xF00FU;
    if (reg >= 0xB000U && reg <= 0xE003U && (reg & 0x0FU) < 4U) {
        const unsigned group = reg >> 12U;
        const unsigned port = reg & 3U;
        auto& bank = registers_.chr_banks[(group - 0xBU) * 2U + port / 2U];
        if ((port & 1U) == 0U) {
            bank = (bank & 0x01F0U) | (value & 0x0FU);
        } else {
            bank = (bank & 0x000FU) | ((value & 0x1FU) << 4U);
        }
        return;
    }

    switch (reg) {
    case 0x8000U:
        registers_.prg_banks[0U] = value & 0x1FU;
        break;
    case 0x9000U: {
        constexpr std::array kModes = {MirrorMode::Vertical,
                                       MirrorMode::Horizontal,
                                       MirrorMode::SingleLower,
                                       MirrorMode::SingleUpper};
        registers_.mirror = kModes[value & 3U];
        break;
    }
    case 0x9002U:
        registers_.prg_control = value & 3U;
        break;
    case 0xA000U:
        registers_.prg_banks[1U] = value & 0x1FU;
        break;
    case 0xF000U:
        registers_.irq_latch = (registers_.irq_latch & 0xF0U) | (value & 0x0FU);
        break;
    case 0xF001U:
        registers_.irq_latch = (registers_.irq_latch & 0x0FU) | ((value & 0x0FU) << 4U);
        break;
    case 0xF002U:
        write_irq_control(value);
        break;
    case 0xF003U:
        acknowledge_irq_register();
        break;
    default:
        break;
    }
}

bool Mapper398::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper398::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::uint16_t Mapper398::select_chr_bank(Address addr) {
    if (addr >= 0x2000U) {
        return 0U;
    }
    const auto bank = registers_.chr_banks[(addr >> 10U) & 7U];
    registers_.prg_chr_a12 = (bank >> 4U) & 1U;
    return bank;
}

std::size_t Mapper398::chr_offset(Address addr) {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const auto bank = select_chr_bank(pattern_addr);
    if ((registers_.outer & kOuterMode) != 0U) {
        return kSecondChrChip + ((registers_.outer & kOuterA) != 0U ? 0x010000UZ : 0UZ) +
               static_cast<std::size_t>((bank >> 2U) & 7U) * 0x2000UZ + (pattern_addr & 0x1FFFU);
    }
    const auto inner = static_cast<std::size_t>(bank & 0x01BFU) * kChrBankSize;
    return inner + ((registers_.outer & kOuterA) != 0U ? 0x010000UZ : 0UZ) +
           (pattern_addr & (kChrBankSize - 1U));
}

Byte Mapper398::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper398::write_chr(Address addr, Byte) {
    static_cast<void>(select_chr_bank(static_cast<Address>(addr & 0x1FFFU)));
}

void Mapper398::observe_ppu_address(Address addr) {
    static_cast<void>(select_chr_bank(addr));
}

MirrorMode Mapper398::mirror_mode() const {
    return registers_.mirror;
}

void Mapper398::write_irq_control(Byte value) {
    registers_.irq_pending = 0U;
    registers_.irq_prescaler = kIrqPrescalerReset;
    registers_.irq_control = value & 7U;
    if ((registers_.irq_control & 2U) != 0U) {
        registers_.irq_counter = registers_.irq_latch;
    }
}

void Mapper398::acknowledge_irq_register() {
    registers_.irq_pending = 0U;
    registers_.irq_control = (registers_.irq_control & 5U) | ((registers_.irq_control & 1U) << 1U);
}

void Mapper398::tick_irq() {
    if (registers_.irq_counter == 0xFFU) {
        registers_.irq_counter = registers_.irq_latch;
        registers_.irq_pending = 1U;
    } else {
        ++registers_.irq_counter;
    }
}

void Mapper398::clock_cpu() {
    if ((registers_.irq_control & 2U) == 0U) {
        return;
    }
    if ((registers_.irq_control & 4U) != 0U) {
        tick_irq();
    } else if (registers_.irq_prescaler <= 3U) {
        registers_.irq_prescaler += kIrqPrescalerReset - 3U;
        tick_irq();
    } else {
        registers_.irq_prescaler -= 3U;
    }
}

bool Mapper398::irq_pending() const {
    return registers_.irq_pending != 0U;
}

void Mapper398::acknowledge_irq() {
    registers_.irq_pending = 0U;
}

void Mapper398::power_on() {
    reset();
}

void Mapper398::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

std::uint16_t Mapper398::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper398::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write_array(registers_.prg_banks);
    writer.write_array(registers_.chr_banks);
    writer.write(registers_.mirror);
    writer.write(registers_.prg_control);
    writer.write(registers_.irq_latch);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_prescaler);
    writer.write(registers_.irq_control);
    writer.write(registers_.irq_pending);
    writer.write(registers_.prg_chr_a12);
}

void Mapper398::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    reader.read(next.mirror);
    reader.read(next.prg_control);
    reader.read(next.irq_latch);
    reader.read(next.irq_counter);
    reader.read(next.irq_prescaler);
    reader.read(next.irq_control);
    reader.read(next.irq_pending);
    reader.read(next.prg_chr_a12);

    const bool valid_mirror =
        next.mirror != MirrorMode::FourScreen && next.mirror <= MirrorMode::SingleUpper;
    const bool valid_chr_banks =
        std::ranges::all_of(next.chr_banks, [](std::uint16_t bank) { return bank <= 0x01FFU; });
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (next.outer & ~kOuterMask) != 0U ||
        next.prg_banks[0U] > 0x1FU || next.prg_banks[1U] > 0x1FU || !valid_chr_banks ||
        !valid_mirror || next.prg_control > 3U || next.irq_prescaler == 0U ||
        next.irq_prescaler > kIrqPrescalerReset || next.irq_control > 7U || next.irq_pending > 1U ||
        (next.irq_pending != 0U && (next.irq_control & 2U) == 0U) || next.prg_chr_a12 > 1U) {
        reader.fail();
        return;
    }
    registers_ = next;
}

} // namespace mapperbus::core
