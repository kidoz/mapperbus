#include "core/mappers/mapper473.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x00008000UZ;
constexpr std::size_t kMaxPrgSize = 0x02000000UZ;
constexpr std::size_t kMinChrSize = 0x00002000UZ;
constexpr std::size_t kMaxChrSize = 0x00040000UZ;
constexpr std::size_t kPrgBankSize = 0x00002000UZ;
constexpr std::size_t kChrBankSize = 0x00000400UZ;
constexpr std::size_t kNromChrBankSize = 0x00002000UZ;
constexpr std::size_t kPrgRamSize = 0x00002000UZ;
constexpr std::uint16_t kStateTag = 0x4730U;

} // namespace

// Original implementation from NESdev mapper 473 r23359 and its linked KJ01A-18
// register description. Cold power clears volatile RAM; soft reset preserves it.
Mapper473::Mapper473(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), chr_ram_(header.chr_ram_size),
      has_prg_ram_(header.prg_ram_size == kPrgRamSize), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper473::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 473U) {
        return std::unexpected("Mapper 473 requires mapper ID 473");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 473 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize) {
        return std::unexpected("Mapper 473 requires power-of-two 32 KiB-32 MiB PRG ROM");
    }
    const bool chr_rom = chr_size != 0U;
    const bool chr_ram = header.chr_ram_size != 0U;
    if (chr_rom == chr_ram ||
        (chr_rom &&
         (!std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize)) ||
        (chr_ram && (!std::has_single_bit(header.chr_ram_size) ||
                     header.chr_ram_size < kMinChrSize || header.chr_ram_size > kMaxChrSize))) {
        return std::unexpected(
            "Mapper 473 requires exactly one power-of-two 8-256 KiB CHR ROM or CHR RAM device");
    }
    if ((header.prg_ram_size != 0U && header.prg_ram_size != kPrgRamSize) ||
        header.prg_nvram_size != 0U || header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 473 supports only optional 8 KiB volatile PRG RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 473 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 473 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper473::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if (slot == 1U) {
        return registers_.bank_registers[7U] & 0x3FU;
    }
    if ((!registers_.prg_mode && slot == 0U) || (registers_.prg_mode && slot == 2U)) {
        return registers_.bank_registers[6U] & 0x3FU;
    }
    return slot == 3U ? 63U : 62U;
}

unsigned Mapper473::outer_prg_bank(unsigned inner_bank) const {
    const Byte control = registers_.outer[0U];
    const unsigned mask = (control & 0x20U) != 0U ? 0xFFU >> (7U - (control & 7U)) : 0U;
    const unsigned base = (control & 0x20U) != 0U
                              ? static_cast<unsigned>(registers_.outer[1U]) |
                                    (static_cast<unsigned>(registers_.outer[2U]) << 8U)
                              : 63U;
    return (base & ~mask) | (inner_bank & mask);
}

Byte Mapper473::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!has_prg_ram_ || !registers_.prg_ram_enabled) {
            return 0U;
        }
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto bank = outer_prg_bank(inner_prg_bank(addr));
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper473::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (has_prg_ram_ && registers_.prg_ram_enabled && !registers_.prg_ram_write_protected) {
            prg_ram_[addr & 0x1FFFU] = value;
        }
        return;
    }
    if (addr < 0x8000U || (registers_.outer[0U] & 0x40U) != 0U) {
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
        registers_.prg_ram_enabled = (value & 0x80U) != 0U;
        registers_.prg_ram_write_protected = (value & 0x40U) != 0U;
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

bool Mapper473::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && has_prg_ram_ && registers_.prg_ram_enabled);
}

Byte Mapper473::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper473::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (registers_.chr_inversion ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.bank_registers[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.bank_registers[slot - 2U];
}

std::size_t Mapper473::chr_offset(Address addr) const {
    if ((registers_.outer[0U] & 0x80U) == 0U) {
        return static_cast<std::size_t>(registers_.outer[3U]) * kNromChrBankSize + (addr & 0x1FFFU);
    }
    return static_cast<std::size_t>(inner_chr_bank(addr)) * kChrBankSize + (addr & 0x03FFU);
}

Byte Mapper473::read_chr(Address addr) {
    if (!chr_rom_.empty()) {
        return chr_rom_[chr_offset(addr) % chr_rom_.size()];
    }
    if (!chr_ram_.empty()) {
        return chr_ram_[chr_offset(addr) % chr_ram_.size()];
    }
    return 0U;
}

void Mapper473::write_chr(Address addr, Byte value) {
    if (!chr_ram_.empty()) {
        chr_ram_[chr_offset(addr) % chr_ram_.size()] = value;
    }
}

MirrorMode Mapper473::mirror_mode() const {
    return registers_.mirror_mode;
}

bool Mapper473::maps_expansion(Address addr) const {
    return addr >= 0x4800U && addr < 0x5000U;
}

Byte Mapper473::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0U : 0xFFU;
}

void Mapper473::write_expansion(Address addr, Byte value) {
    if (maps_expansion(addr)) {
        registers_.outer[addr & 3U] = value;
    }
}

void Mapper473::power_on() {
    reset();
    prg_ram_.fill(0U);
    std::fill(chr_ram_.begin(), chr_ram_.end(), 0U);
}

void Mapper473::reset() {
    registers_ = {};
    registers_.mirror_mode = initial_mirror_;
}

bool Mapper473::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper473::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper473::clock_irq_counter() {
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

std::uint16_t Mapper473::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (has_prg_ram_ ? 2U : 0U) | (!chr_ram_.empty() ? 4U : 0U));
}

void Mapper473::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_ram_.size()));
    writer.write_array(registers_.outer);
    writer.write(registers_.bank_select);
    writer.write<Byte>((registers_.prg_mode ? 0x40U : 0U) |
                       (registers_.chr_inversion ? 0x80U : 0U));
    writer.write_array(registers_.bank_registers);
    writer.write(static_cast<Byte>(registers_.mirror_mode));
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write<Byte>((registers_.irq_reload_pending ? 1U : 0U) |
                       (registers_.irq_enabled ? 2U : 0U) | (registers_.irq_pending ? 4U : 0U));
    writer.write<Byte>((registers_.prg_ram_enabled ? 0x80U : 0U) |
                       (registers_.prg_ram_write_protected ? 0x40U : 0U));
    if (has_prg_ram_) {
        writer.write_array(prg_ram_);
    }
    if (!chr_ram_.empty()) {
        writer.write_bytes(chr_ram_);
    }
}

void Mapper473::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_rom_size = reader.read<std::uint32_t>();
    const auto chr_ram_size = reader.read<std::uint32_t>();
    Registers next{};
    reader.read_array(next.outer);
    reader.read(next.bank_select);
    const auto mode = reader.read<Byte>();
    reader.read_array(next.bank_registers);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    std::array<Byte, kPrgRamSize> prg_ram{};
    if (has_prg_ram_) {
        reader.read_array(prg_ram);
    }
    std::vector<Byte> chr_ram;
    if (!chr_ram_.empty()) {
        reader.read_bytes(chr_ram, chr_ram_.size());
    }

    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_rom_size == chr_rom_.size() && chr_ram_size == chr_ram_.size() &&
                       next.bank_select <= 7U && (mode & 0x3FU) == 0U &&
                       next.bank_registers[6U] <= 0x3FU && next.bank_registers[7U] <= 0x3FU &&
                       mirror <= static_cast<Byte>(MirrorMode::Vertical) && irq <= 7U &&
                       ((irq & 4U) == 0U || (irq & 2U) != 0U) && (ram & 0x3FU) == 0U &&
                       chr_ram.size() == chr_ram_.size();
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
    next.prg_ram_enabled = (ram & 0x80U) != 0U;
    next.prg_ram_write_protected = (ram & 0x40U) != 0U;
    registers_ = next;
    if (has_prg_ram_) {
        prg_ram_ = prg_ram;
    }
    if (!chr_ram_.empty()) {
        chr_ram_ = std::move(chr_ram);
    }
}

} // namespace mapperbus::core
