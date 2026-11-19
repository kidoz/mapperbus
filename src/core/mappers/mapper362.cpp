#include "core/mappers/mapper362.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kRomSize = 0x080000UZ;
constexpr std::size_t kOuterBankSize = 0x020000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x3620U;
constexpr std::uint16_t kIrqPrescalerReset = 341U;

} // namespace

// NESdev mapper 362 oldid 7956 specifies VRC4f wiring, seven inner CHR bits,
// two per-slot outer bits, and acknowledge-only F003. Reset clears all latches;
// that deterministic policy does not claim measured electrical startup values.
Mapper362::Mapper362(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper362::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 362U) {
        return std::unexpected("Mapper 362 requires mapper ID 362");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 362 requires NES 2.0 submapper 0");
    }
    if (prg_size != kRomSize || chr_size != kRomSize) {
        return std::unexpected("Mapper 362 requires exactly 512 KiB PRG and CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 362 requires ROM-only memory with no battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 362 requires ordinary H/V initial CIRAM metadata");
    }
    return {};
}

Byte Mapper362::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (registers_.prg_control & 2U) != 0U;
    unsigned bank = 15U;
    if (slot == 1U) {
        bank = registers_.prg_banks[1U] & 0x0FU;
    } else if (slot == 0U || slot == 2U) {
        bank = (slot == 0U) != swapped ? registers_.prg_banks[0U] & 0x0FU : 14U;
    }
    const auto offset = static_cast<std::size_t>(registers_.outer_bank) * kOuterBankSize +
                        bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper362::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
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
        registers_.irq_pending = 0U;
        break;
    default:
        break;
    }
}

bool Mapper362::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper362::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

void Mapper362::select_outer_bank(Address addr) {
    if (addr >= 0x2000U) {
        return;
    }
    const unsigned slot = (addr >> 10U) & 7U;
    registers_.outer_bank = static_cast<Byte>((registers_.chr_banks[slot] >> 7U) & 3U);
}

std::size_t Mapper362::chr_offset(Address addr) {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    select_outer_bank(pattern_addr);
    const unsigned slot = pattern_addr / kChrBankSize;
    const auto bank = registers_.chr_banks[slot];
    const auto offset = static_cast<std::size_t>((bank >> 7U) & 3U) * kOuterBankSize +
                        (bank & 0x7FU) * kChrBankSize + (pattern_addr & (kChrBankSize - 1U));
    return offset % chr_rom_.size();
}

Byte Mapper362::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr)];
}

void Mapper362::write_chr(Address addr, Byte) {
    if (!chr_rom_.empty()) {
        static_cast<void>(chr_offset(addr));
    }
}

void Mapper362::observe_ppu_address(Address addr) {
    select_outer_bank(addr);
}

MirrorMode Mapper362::mirror_mode() const {
    return registers_.mirror;
}

void Mapper362::write_irq_control(Byte value) {
    registers_.irq_pending = 0U;
    registers_.irq_prescaler = kIrqPrescalerReset;
    registers_.irq_control = value & 7U;
    if ((registers_.irq_control & 2U) != 0U) {
        registers_.irq_counter = registers_.irq_latch;
    }
}

void Mapper362::tick_irq() {
    if (registers_.irq_counter == 0xFFU) {
        registers_.irq_counter = registers_.irq_latch;
        registers_.irq_pending = 1U;
    } else {
        ++registers_.irq_counter;
    }
}

void Mapper362::clock_cpu() {
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

bool Mapper362::irq_pending() const {
    return registers_.irq_pending != 0U;
}

void Mapper362::acknowledge_irq() {
    registers_.irq_pending = 0U;
}

void Mapper362::power_on() {
    reset();
}

void Mapper362::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

void Mapper362::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(initial_mirror_);
    writer.write_array(registers_.prg_banks);
    writer.write_array(registers_.chr_banks);
    writer.write(registers_.outer_bank);
    writer.write(registers_.mirror);
    writer.write(registers_.prg_control);
    writer.write(registers_.irq_latch);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_prescaler);
    writer.write(registers_.irq_control);
    writer.write(registers_.irq_pending);
}

void Mapper362::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<MirrorMode>();
    Registers next;
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    reader.read(next.outer_bank);
    reader.read(next.mirror);
    reader.read(next.prg_control);
    reader.read(next.irq_latch);
    reader.read(next.irq_counter);
    reader.read(next.irq_prescaler);
    reader.read(next.irq_control);
    reader.read(next.irq_pending);

    const bool valid_mirror =
        next.mirror != MirrorMode::FourScreen && next.mirror <= MirrorMode::SingleUpper;
    const bool valid_chr_banks =
        std::ranges::all_of(next.chr_banks, [](std::uint16_t bank) { return bank <= 0x01FFU; });
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || initial_mirror != initial_mirror_ ||
        next.prg_banks[0U] > 0x1FU || next.prg_banks[1U] > 0x1FU || !valid_chr_banks ||
        next.outer_bank > 3U || !valid_mirror || next.prg_control > 3U ||
        next.irq_prescaler == 0U || next.irq_prescaler > kIrqPrescalerReset ||
        next.irq_control > 7U || next.irq_pending > 1U ||
        (next.irq_pending != 0U && (next.irq_control & 2U) == 0U)) {
        reader.fail();
        return;
    }
    registers_ = next;
}

} // namespace mapperbus::core
