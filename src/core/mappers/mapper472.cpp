#include "core/mappers/mapper472.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x4720U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 472 oldid 23358.
// Reset values and DIP toggling are deterministic compatibility policies.
Mapper472::Mapper472(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper472::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 472U) {
        return std::unexpected("Mapper 472 requires mapper ID 472");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 472 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 472 requires power-of-two 32 KiB-2 MiB PRG and 8 KiB-2 MiB CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 472 has no cartridge RAM or nonvolatile storage");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 472 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 472 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper472::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if (slot == 1U) {
        return registers_.bank_registers[7U] & 0x3FU;
    }
    if ((!registers_.prg_mode && slot == 0U) || (registers_.prg_mode && slot == 2U)) {
        return registers_.bank_registers[6U] & 0x3FU;
    }
    return slot == 3U ? 31U : 30U;
}

unsigned Mapper472::outer_prg_bank(unsigned inner_bank) const {
    return (registers_.outer_bank & 0xF0U) | (inner_bank & 0x0FU);
}

Byte Mapper472::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return registers_.dip_switches;
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto bank = outer_prg_bank(inner_prg_bank(addr));
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper472::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (addr >= 0x6000U && registers_.outer_writes_enabled &&
            !registers_.outer_write_protected) {
            registers_.outer_bank = value;
        }
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.bank_select = value & 7U;
        registers_.prg_mode = (value & 0x40U) != 0U;
        registers_.chr_inversion = (value & 0x80U) != 0U;
        break;
    case 0x8001U:
        registers_.bank_registers[registers_.bank_select] =
            registers_.bank_select >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        break;
    case 0xA000U:
        registers_.mirror_mode = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        registers_.outer_writes_enabled = (value & 0x80U) != 0U;
        registers_.outer_write_protected = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        registers_.irq_reload = value;
        break;
    case 0xC001U:
        registers_.irq_reload_pending = true;
        registers_.irq_counter = 0U;
        break;
    case 0xE000U:
        registers_.irq_enabled = false;
        registers_.irq_pending = false;
        break;
    case 0xE001U:
        registers_.irq_enabled = true;
        break;
    default:
        break;
    }
}

bool Mapper472::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper472::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper472::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (registers_.chr_inversion ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.bank_registers[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.bank_registers[slot - 2U];
}

unsigned Mapper472::outer_chr_bank(unsigned inner_bank) const {
    if ((registers_.outer_bank & 0x20U) != 0U) {
        return (inner_bank & 0x7FU) | ((registers_.outer_bank & 0xF0U) << 3U);
    }
    return (inner_bank & 0xFFU) | ((registers_.outer_bank & 0xE0U) << 3U);
}

Byte Mapper472::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto bank = outer_chr_bank(inner_chr_bank(addr));
    const auto offset = static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper472::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper472::mirror_mode() const {
    return registers_.mirror_mode;
}

void Mapper472::power_on() {
    registers_ = {};
    registers_.dip_switches = 0x80U;
    registers_.mirror_mode = initial_mirror_;
}

void Mapper472::reset() {
    const Byte dip_switches = registers_.dip_switches ^ 0x80U;
    registers_ = {};
    registers_.dip_switches = dip_switches;
    registers_.mirror_mode = initial_mirror_;
}

bool Mapper472::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper472::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper472::clock_irq_counter() {
    if (registers_.irq_counter == 0U || registers_.irq_reload_pending) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_reload_pending = false;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && registers_.irq_enabled) {
        registers_.irq_pending = true;
    }
}

std::uint16_t Mapper472::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper472::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer_bank);
    writer.write(registers_.dip_switches);
    writer.write(registers_.bank_select);
    writer.write<Byte>((registers_.prg_mode ? 0x40U : 0U) |
                       (registers_.chr_inversion ? 0x80U : 0U));
    writer.write_array(registers_.bank_registers);
    writer.write(static_cast<Byte>(registers_.mirror_mode));
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write<Byte>((registers_.irq_reload_pending ? 1U : 0U) |
                       (registers_.irq_enabled ? 2U : 0U) | (registers_.irq_pending ? 4U : 0U));
    writer.write<Byte>((registers_.outer_writes_enabled ? 0x80U : 0U) |
                       (registers_.outer_write_protected ? 0x40U : 0U));
}

void Mapper472::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next{};
    reader.read(next.outer_bank);
    reader.read(next.dip_switches);
    reader.read(next.bank_select);
    const auto mode = reader.read<Byte>();
    reader.read_array(next.bank_registers);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    const auto irq = reader.read<Byte>();
    const auto outer_control = reader.read<Byte>();

    const bool valid =
        reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
        chr_size == chr_rom_.size() && (next.dip_switches == 0U || next.dip_switches == 0x80U) &&
        next.bank_select <= 7U && (mode & 0x3FU) == 0U && next.bank_registers[6U] <= 0x3FU &&
        next.bank_registers[7U] <= 0x3FU && mirror <= static_cast<Byte>(MirrorMode::Vertical) &&
        irq <= 7U && ((irq & 4U) == 0U || (irq & 2U) != 0U) && (outer_control & 0x3FU) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    next.prg_mode = (mode & 0x40U) != 0U;
    next.chr_inversion = (mode & 0x80U) != 0U;
    next.mirror_mode = static_cast<MirrorMode>(mirror);
    next.irq_reload_pending = (irq & 1U) != 0U;
    next.irq_enabled = (irq & 2U) != 0U;
    next.irq_pending = (irq & 4U) != 0U;
    next.outer_writes_enabled = (outer_control & 0x80U) != 0U;
    next.outer_write_protected = (outer_control & 0x40U) != 0U;
    registers_ = next;
}

} // namespace mapperbus::core
