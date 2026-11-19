#include "core/mappers/mapper445.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x100000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4450U;
constexpr Byte kOuterBankMask = 0x70U;
constexpr Byte kOuterModeMask = 0x05U;
constexpr Byte kOuterPrgA17Mode = 0x01U;
constexpr Byte kOuterChrA17Mode = 0x04U;
constexpr Byte kOuterLock = 0x20U;

} // namespace

// NESdev mapper 445 oldid 19271 does not specify electrical reset values.
// Clearing outer/MMC3 registers and the lock is a deterministic reset policy.
Mapper445::Mapper445(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_size_(header.prg_ram_size + header.prg_nvram_size),
      nvram_size_(header.prg_nvram_size), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper445::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 445U) {
        return std::unexpected("Mapper 445 requires mapper ID 445");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 445 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected("Mapper 445 requires 32 KiB-1 MiB PRG in 8 KiB units and 8 KiB-1 "
                               "MiB CHR in 1 KiB units");
    }
    if ((header.prg_ram_size != 0U && header.prg_ram_size != kWramSize) ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kWramSize) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
        header.has_battery != (header.prg_nvram_size != 0U) || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 445 supports no CPU RAM or one 8 KiB PRG RAM/NVRAM chip and CHR ROM only");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 445 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 445 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper445::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if (slot == 1U) {
        return registers_.bank_registers[7U] & 0x3FU;
    }
    if ((!registers_.prg_mode && slot == 0U) || (registers_.prg_mode && slot == 2U)) {
        return registers_.bank_registers[6U] & 0x3FU;
    }
    return slot == 3U ? 63U : 62U;
}

Byte Mapper445::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!registers_.prg_ram_enabled || prg_ram_size_ == 0U) {
            return 0U;
        }
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const unsigned inner = inner_prg_bank(addr);
    const unsigned a17 = (registers_.outer_mode & kOuterPrgA17Mode) != 0U
                             ? registers_.outer_prg & 0x10U
                             : inner & 0x10U;
    const unsigned bank = (inner & 0x0FU) | a17 | (registers_.outer_prg & 0x60U);
    const auto offset = bank * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper445::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (registers_.prg_ram_enabled && !registers_.prg_ram_write_protected &&
            prg_ram_size_ != 0U) {
            prg_ram_[addr & 0x1FFFU] = value;
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

bool Mapper445::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && registers_.prg_ram_enabled && prg_ram_size_ != 0U);
}

Byte Mapper445::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper445::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (registers_.chr_inversion ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.bank_registers[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.bank_registers[slot - 2U];
}

Byte Mapper445::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const unsigned inner = inner_chr_bank(addr);
    const unsigned a17 = (registers_.outer_mode & kOuterChrA17Mode) != 0U
                             ? (registers_.outer_chr & 0x10U) << 3U
                             : inner & 0x80U;
    const unsigned bank = (inner & 0x7FU) | a17 | ((registers_.outer_chr & 0x60U) << 3U);
    const auto offset = bank * kChrBankSize + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper445::write_chr(Address, Byte) {}

MirrorMode Mapper445::mirror_mode() const {
    return registers_.mirror_mode;
}

bool Mapper445::maps_expansion(Address addr) const {
    const auto decoded = addr & 0xF003U;
    return decoded >= 0x5000U && decoded <= 0x5003U;
}

Byte Mapper445::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0U : 0xFFU;
}

Byte Mapper445::read_expansion(Address) {
    return 0U;
}

void Mapper445::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr) || registers_.locked) {
        return;
    }
    switch (addr & 3U) {
    case 0U:
        registers_.outer_prg = value & kOuterBankMask;
        break;
    case 1U:
        registers_.outer_chr = value & kOuterBankMask;
        break;
    case 2U:
        registers_.outer_mode = value & kOuterModeMask;
        break;
    case 3U:
        registers_.locked = (value & kOuterLock) != 0U;
        break;
    default:
        std::unreachable();
    }
}

void Mapper445::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        prg_ram_.fill(0U);
    }
}

void Mapper445::reset() {
    registers_ = {};
    registers_.mirror_mode = initial_mirror_;
}

bool Mapper445::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper445::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper445::clock_irq_counter() {
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

std::uint16_t Mapper445::memory_tag() const {
    return static_cast<std::uint16_t>((initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (prg_ram_size_ != 0U ? 2U : 0U) |
                                      (nvram_size_ != 0U ? 4U : 0U));
}

void Mapper445::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_tag());
    writer.write(registers_.outer_prg);
    writer.write(registers_.outer_chr);
    writer.write(registers_.outer_mode);
    writer.write<Byte>(registers_.locked ? 1U : 0U);
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
    if (prg_ram_size_ != 0U) {
        writer.write_array(prg_ram_);
    }
}

void Mapper445::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory = reader.read<std::uint16_t>();
    Registers next{};
    reader.read(next.outer_prg);
    reader.read(next.outer_chr);
    reader.read(next.outer_mode);
    const auto lock = reader.read<Byte>();
    reader.read(next.bank_select);
    const auto bank_mode = reader.read<Byte>();
    reader.read_array(next.bank_registers);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    std::array<Byte, kWramSize> prg_ram{};
    if (prg_ram_size_ != 0U) {
        reader.read_array(prg_ram);
    }

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory != memory_tag() ||
        (next.outer_prg & ~kOuterBankMask) != 0U || (next.outer_chr & ~kOuterBankMask) != 0U ||
        (next.outer_mode & ~kOuterModeMask) != 0U || lock > 1U || next.bank_select > 7U ||
        (bank_mode & 0x3FU) != 0U || next.bank_registers[6U] > 0x3FU ||
        next.bank_registers[7U] > 0x3FU || mirror > static_cast<Byte>(MirrorMode::Vertical) ||
        irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    next.locked = lock != 0U;
    next.prg_mode = (bank_mode & 0x40U) != 0U;
    next.chr_inversion = (bank_mode & 0x80U) != 0U;
    next.mirror_mode = static_cast<MirrorMode>(mirror);
    next.irq_reload_pending = (irq & 1U) != 0U;
    next.irq_enabled = (irq & 2U) != 0U;
    next.irq_pending = (irq & 4U) != 0U;
    next.prg_ram_enabled = (ram & 0x80U) != 0U;
    next.prg_ram_write_protected = (ram & 0x40U) != 0U;
    registers_ = next;
    if (prg_ram_size_ != 0U) {
        prg_ram_ = prg_ram;
    }
}

std::span<const Byte> Mapper445::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper445::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), nvram_size_), prg_ram_.begin());
}

} // namespace mapperbus::core
