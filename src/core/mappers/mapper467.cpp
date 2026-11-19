#include "core/mappers/mapper467.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kNromBankSize = 0x004000UZ;
constexpr std::uint16_t kStateTag = 0x4670U;
constexpr Byte kPrgMode = 0x20U;
constexpr Byte kWideBanking = 0x40U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 467 oldid 19326.
// The source marks the $F000 outer-register mask as tentative. Zeroed reset
// latches and the existing repeating-zero MMC3 IRQ policy are deterministic.
Mapper467::Mapper467(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> Mapper467::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 467U) {
        return std::unexpected("Mapper 467 requires mapper ID 467");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 467 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxRomSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxRomSize) {
        return std::unexpected(
            "Mapper 467 requires power-of-two 32 KiB-1 MiB PRG and 8 KiB-1 MiB CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 467 has no cartridge RAM or nonvolatile storage");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 467 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 467 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper467::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool wide = (registers_.outer & kWideBanking) != 0U;
    const unsigned mask = wide ? 0x0FU : 0x03U;
    if (slot == 1U) {
        return registers_.bank_registers[7U] & mask;
    }
    if (slot == 0U) {
        return registers_.bank_registers[6U] & mask;
    }
    return wide ? (slot == 3U ? 15U : 14U) : (slot == 3U ? 3U : 2U);
}

std::size_t Mapper467::prg_offset(Address addr) const {
    const auto outer = registers_.outer;
    if ((outer & kPrgMode) == 0U) {
        return static_cast<std::size_t>(outer & 0x1FU) * kNromBankSize +
               (addr & (kNromBankSize - 1U));
    }

    const std::size_t outer_base = (outer & kWideBanking) != 0U
                                       ? static_cast<std::size_t>(outer & 0x38U) << 14U
                                       : static_cast<std::size_t>(outer & 0x3EU) << 14U;
    return outer_base + inner_prg_bank(addr) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper467::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper467::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if ((addr & 0xF000U) == 0x9000U) {
        registers_.outer = value;
        return;
    }

    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            registers_.bank_select = value & 7U;
        } else {
            const unsigned index = registers_.bank_select;
            registers_.bank_registers[index] =
                index >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        }
    } else if (addr >= 0xC000U && addr < 0xE000U) {
        if (even) {
            registers_.irq_latch = value;
        } else {
            registers_.irq_counter = 0U;
            registers_.irq_reload = true;
        }
    } else if (addr >= 0xE000U) {
        if (even) {
            registers_.irq_enabled = false;
            registers_.irq_pending = false;
        } else {
            registers_.irq_enabled = true;
        }
    }
}

bool Mapper467::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper467::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper467::inner_chr_bank(Address addr) const {
    const unsigned slot = (addr >> 10U) & 7U;
    if (slot < 4U) {
        return (registers_.bank_registers[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.bank_registers[slot - 2U];
}

std::size_t Mapper467::chr_offset(Address addr) const {
    const std::size_t bank = inner_chr_bank(addr);
    if ((registers_.outer & kWideBanking) == 0U) {
        return (bank >> 2U) * 0x2000U + (addr & 0x1FFFU);
    }
    return 0x80000UZ + bank * 0x800U + (addr & 0x7FFU);
}

Byte Mapper467::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper467::write_chr(Address, Byte) {}

MirrorMode Mapper467::mirror_mode() const {
    return (registers_.outer & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper467::power_on() {
    reset();
}

void Mapper467::reset() {
    registers_ = {};
}

bool Mapper467::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper467::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper467::clock_irq_counter() {
    if (registers_.irq_counter == 0U || registers_.irq_reload) {
        registers_.irq_counter = registers_.irq_latch;
        registers_.irq_reload = false;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && registers_.irq_enabled) {
        registers_.irq_pending = true;
    }
}

void Mapper467::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.bank_select);
    writer.write_array(registers_.bank_registers);
    writer.write(registers_.irq_latch);
    writer.write(registers_.irq_counter);
    writer.write<Byte>((registers_.irq_reload ? 1U : 0U) | (registers_.irq_enabled ? 2U : 0U) |
                       (registers_.irq_pending ? 4U : 0U));
}

void Mapper467::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next{};
    reader.read(next.outer);
    reader.read(next.bank_select);
    reader.read_array(next.bank_registers);
    reader.read(next.irq_latch);
    reader.read(next.irq_counter);
    const auto irq = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || next.bank_select > 7U || next.bank_registers[6U] > 0x3FU ||
        next.bank_registers[7U] > 0x3FU || irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U)) {
        reader.fail();
        return;
    }

    next.irq_reload = (irq & 1U) != 0U;
    next.irq_enabled = (irq & 2U) != 0U;
    next.irq_pending = (irq & 4U) != 0U;
    registers_ = next;
}

} // namespace mapperbus::core
