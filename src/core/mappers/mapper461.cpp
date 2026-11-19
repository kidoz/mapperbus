#include "core/mappers/mapper461.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x040000UZ;
constexpr std::size_t kLargeChrSize = 0x040000UZ;
constexpr std::size_t kSmallChrSize = 0x008000UZ;
constexpr std::size_t kChrSize = kLargeChrSize + kSmallChrSize;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::uint16_t kStateTag = 0x4610U;

bool valid_shift_register(Byte shift, Byte count) {
    if (count > 4U || shift > 0x1FU) {
        return false;
    }
    const auto sentinel = static_cast<Byte>(0x10U >> count);
    return (shift & sentinel) != 0U && (shift & (sentinel - 1U)) == 0U;
}

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 461 oldid 21169.
Mapper461::Mapper461(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> Mapper461::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 461U) {
        return std::unexpected("Mapper 461 requires mapper ID 461");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 461 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != kChrSize) {
        return std::unexpected("Mapper 461 requires 256 KiB PRG and 256+32 KiB CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 461 has no RAM or nonvolatile storage");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 461 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 461 does not provide trainer RAM");
    }
    return {};
}

bool Mapper461::nrom_mode() const {
    return (registers_.outer & 0x04U) == 0U;
}

std::size_t Mapper461::prg_offset(Address addr) const {
    const unsigned outer = registers_.outer;
    const std::size_t outer_base =
        ((outer >> 3U) & 1U) * 0x020000UZ + ((outer >> 2U) & 1U) * 0x010000UZ;
    if (nrom_mode()) {
        return outer_base + (outer & 3U) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }

    const unsigned bank_count = (outer & 0x08U) != 0U ? 4U : 8U;
    const unsigned selected = registers_.prg_bank & 0x0FU;
    const unsigned mode = (registers_.control >> 2U) & 3U;
    unsigned bank = 0U;
    if (mode <= 1U) {
        bank = (selected & 0x0EU) | (addr >= 0xC000U ? 1U : 0U);
    } else if (mode == 2U) {
        bank = addr < 0xC000U ? 0U : selected;
    } else {
        bank = addr < 0xC000U ? selected : bank_count - 1U;
    }
    bank &= bank_count - 1U;
    return outer_base + bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper461::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper461::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        registers_.outer = value & 0x0FU;
        return;
    }
    if (addr < 0x8000U) {
        return;
    }
    if ((value & 0x80U) != 0U) {
        reset_serial_port();
        return;
    }

    registers_.shift = static_cast<Byte>((registers_.shift >> 1U) | ((value & 1U) << 4U));
    ++registers_.write_count;
    if (registers_.write_count == 5U) {
        write_register(addr, registers_.shift);
        registers_.shift = 0x10U;
        registers_.write_count = 0U;
    }
}

void Mapper461::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (addr < 0x8000U || !consecutive || (value & 0x80U) != 0U) {
        write_prg(addr, value);
    }
}

bool Mapper461::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper461::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper461::chr_offset(Address addr) const {
    const bool upper = (addr & 0x1000U) != 0U;
    unsigned bank = 0U;
    if ((registers_.control & 0x10U) != 0U) {
        bank = upper ? registers_.chr_bank1 : registers_.chr_bank0;
    } else {
        bank = (registers_.chr_bank0 & 0x1EU) | (upper ? 1U : 0U);
    }
    const std::size_t inner = bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    if (nrom_mode()) {
        return kLargeChrSize + inner % kSmallChrSize;
    }
    return ((registers_.outer >> 3U) & 1U) * 0x020000UZ + inner;
}

Byte Mapper461::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper461::write_chr(Address, Byte) {}

MirrorMode Mapper461::mirror_mode() const {
    switch (registers_.control & 3U) {
    case 0U:
        return MirrorMode::SingleLower;
    case 1U:
        return MirrorMode::SingleUpper;
    case 2U:
        return MirrorMode::Vertical;
    default:
        return MirrorMode::Horizontal;
    }
}

void Mapper461::reset_serial_port() {
    registers_.shift = 0x10U;
    registers_.write_count = 0U;
    registers_.control |= 0x0CU;
}

void Mapper461::write_register(Address addr, Byte value) {
    value &= 0x1FU;
    if (addr < 0xA000U) {
        registers_.control = value;
    } else if (addr < 0xC000U) {
        registers_.chr_bank0 = value;
    } else if (addr < 0xE000U) {
        registers_.chr_bank1 = value;
    } else {
        registers_.prg_bank = value;
    }
}

void Mapper461::power_on() {
    registers_ = {};
}

void Mapper461::reset() {
    reset_serial_port();
}

void Mapper461::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.shift);
    writer.write(registers_.write_count);
    writer.write(registers_.control);
    writer.write(registers_.chr_bank0);
    writer.write(registers_.chr_bank1);
    writer.write(registers_.prg_bank);
}

void Mapper461::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next{};
    reader.read(next.outer);
    reader.read(next.shift);
    reader.read(next.write_count);
    reader.read(next.control);
    reader.read(next.chr_bank0);
    reader.read(next.chr_bank1);
    reader.read(next.prg_bank);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || next.outer > 0x0FU ||
        !valid_shift_register(next.shift, next.write_count) || next.control > 0x1FU ||
        next.chr_bank0 > 0x1FU || next.chr_bank1 > 0x1FU || next.prg_bank > 0x1FU) {
        reader.fail();
        return;
    }
    registers_ = next;
}

} // namespace mapperbus::core
