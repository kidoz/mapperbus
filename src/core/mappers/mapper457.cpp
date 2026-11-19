#include "core/mappers/mapper457.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kSmallOuterSize = 0x020000UZ;
constexpr std::size_t kLargeOuterSize = 0x040000UZ;
constexpr std::uint16_t kStateTag = 0x4570U;
constexpr Byte kOuterMask = 0x0FU;
constexpr Byte kOuterSizeSelect = 0x08U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 457 oldid 21023. MMC3 reset
// values other than the documented zero outer latch are deterministic policies.
Mapper457::Mapper457(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper457::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 457U) {
        return std::unexpected("Mapper 457 requires mapper ID 457");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 457 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || !std::has_single_bit(chr_size) ||
        prg_size < kMinPrgSize || prg_size > kMaxRomSize || chr_size < kMinChrSize ||
        chr_size > kMaxRomSize) {
        return std::unexpected(
            "Mapper 457 requires power-of-two 32 KiB-2 MiB PRG and 8 KiB-2 MiB CHR ROMs");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 457 has no work RAM or battery-backed storage");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 457 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 457 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper457::inner_prg_bank(Address addr) const {
    const unsigned mask = (registers_.outer & kOuterSizeSelect) != 0U ? 0x1FU : 0x0FU;
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if (slot == 1U) {
        return registers_.bank_registers[7U] & mask;
    }
    if ((!registers_.prg_mode && slot == 0U) || (registers_.prg_mode && slot == 2U)) {
        return registers_.bank_registers[6U] & mask;
    }
    return slot == 3U ? mask : mask - 1U;
}

unsigned Mapper457::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (registers_.chr_inversion ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.bank_registers[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.bank_registers[slot - 2U];
}

std::size_t Mapper457::outer_offset() const {
    const std::size_t size =
        (registers_.outer & kOuterSizeSelect) != 0U ? kLargeOuterSize : kSmallOuterSize;
    return (registers_.outer & 7U) * size;
}

std::size_t Mapper457::prg_offset(Address addr) const {
    return outer_offset() + inner_prg_bank(addr) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

std::size_t Mapper457::chr_offset(Address addr) const {
    const unsigned mask = (registers_.outer & kOuterSizeSelect) != 0U ? 0xFFU : 0x7FU;
    return outer_offset() + (inner_chr_bank(addr) & mask) * kChrBankSize +
           (addr & (kChrBankSize - 1U));
}

Byte Mapper457::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper457::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (registers_.prg_ram_enabled) {
            registers_.outer = value & kOuterMask;
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            registers_.bank_select = value & 7U;
            registers_.prg_mode = (value & 0x40U) != 0U;
            registers_.chr_inversion = (value & 0x80U) != 0U;
        } else {
            registers_.bank_registers[registers_.bank_select] =
                registers_.bank_select >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        }
    } else if (addr < 0xC000U) {
        if (even) {
            registers_.mirror_mode =
                (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        } else {
            registers_.prg_ram_enabled = (value & 0x80U) != 0U;
            registers_.prg_ram_write_protected = (value & 0x40U) != 0U;
        }
    } else if (addr < 0xE000U) {
        if (even) {
            registers_.irq_reload = value;
        } else {
            registers_.irq_reload_pending = true;
            registers_.irq_counter = 0U;
        }
    } else if (even) {
        registers_.irq_enabled = false;
        registers_.irq_pending = false;
    } else {
        registers_.irq_enabled = true;
    }
}

bool Mapper457::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper457::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper457::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper457::write_chr(Address, Byte) {}

MirrorMode Mapper457::mirror_mode() const {
    return registers_.mirror_mode;
}

void Mapper457::power_on() {
    reset();
}

void Mapper457::reset() {
    registers_ = {};
    registers_.mirror_mode = initial_mirror_;
}

bool Mapper457::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper457::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper457::clock_irq_counter() {
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

std::uint16_t Mapper457::initial_mirror_tag() const {
    return initial_mirror_ == MirrorMode::Vertical ? 1U : 0U;
}

void Mapper457::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(initial_mirror_tag());
    writer.write(registers_.outer);
    writer.write(registers_.bank_select);
    writer.write<Byte>((registers_.prg_mode ? 0x40U : 0U) |
                       (registers_.chr_inversion ? 0x80U : 0U));
    writer.write_array(registers_.bank_registers);
    writer.write<Byte>(static_cast<Byte>(registers_.mirror_mode));
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write<Byte>((registers_.irq_reload_pending ? 1U : 0U) |
                       (registers_.irq_enabled ? 2U : 0U) | (registers_.irq_pending ? 4U : 0U));
    writer.write<Byte>((registers_.prg_ram_enabled ? 0x80U : 0U) |
                       (registers_.prg_ram_write_protected ? 0x40U : 0U));
}

void Mapper457::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<std::uint16_t>();
    Registers next{};
    reader.read(next.outer);
    reader.read(next.bank_select);
    const auto bank_mode = reader.read<Byte>();
    reader.read_array(next.bank_registers);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || initial_mirror != initial_mirror_tag() ||
        next.outer > kOuterMask || next.bank_select > 7U || (bank_mode & 0x3FU) != 0U ||
        next.bank_registers[6U] > 0x3FU || next.bank_registers[7U] > 0x3FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    next.prg_mode = (bank_mode & 0x40U) != 0U;
    next.chr_inversion = (bank_mode & 0x80U) != 0U;
    next.mirror_mode = static_cast<MirrorMode>(mirror);
    next.irq_reload_pending = (irq & 1U) != 0U;
    next.irq_enabled = (irq & 2U) != 0U;
    next.irq_pending = (irq & 4U) != 0U;
    next.prg_ram_enabled = (ram & 0x80U) != 0U;
    next.prg_ram_write_protected = (ram & 0x40U) != 0U;
    registers_ = next;
}

} // namespace mapperbus::core
