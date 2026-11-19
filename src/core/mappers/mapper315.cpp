#include "core/mappers/mapper315.hpp"

#include <bit>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgBytes = 32U * 1024U;
constexpr std::size_t kMinChrBytes = 8U * 1024U;
constexpr std::size_t kMaxRomBytes = 512U * 1024U;
constexpr std::size_t kPrgBankBytes = 8U * 1024U;
constexpr std::size_t kChrBankBytes = 1024U;
constexpr std::uint16_t kStateTag = 0x3150U;
constexpr Byte kBankSelectMask = 0xC7U;
constexpr Byte kStateFlagsMask = 0x1FU;

Byte mirror_code(MirrorMode mode) {
    switch (mode) {
    case MirrorMode::Horizontal:
        return 0U;
    case MirrorMode::Vertical:
        return 1U;
    case MirrorMode::FourScreen:
        return 2U;
    default:
        return 0xFFU;
    }
}

MirrorMode decode_mirror(Byte code) {
    if (code == 0U) {
        return MirrorMode::Horizontal;
    }
    if (code == 1U) {
        return MirrorMode::Vertical;
    }
    return MirrorMode::FourScreen;
}

} // namespace

Mapper315::Mapper315(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper315::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 315U) {
        return std::unexpected("Mapper315 requires mapper ID 315");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 315 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgBytes || prg_size > kMaxRomBytes ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrBytes || chr_size > kMaxRomBytes) {
        return std::unexpected(
            "Mapper 315 requires power-of-two 32-512 KiB PRG and 8-512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 315 has no cartridge RAM, NVRAM, or battery storage");
    }
    if (mirror_code(header.mirror_mode) == 0xFFU) {
        return std::unexpected("Mapper 315 requires H/V or four-screen nametable metadata");
    }
    return {};
}

Byte Mapper315::prg_bank(Address addr) const {
    const unsigned slot = (addr >> 13U) & 3U;
    const bool prg_mode = (bank_select_ & 0x40U) != 0U;
    const Byte r6 = bank_registers_[6U] & 0x0FU;
    const Byte r7 = bank_registers_[7U] & 0x0FU;
    Byte inner = 0U;

    if ((outer_bank_ & 0x06U) == 0x06U) {
        inner = (slot == 0U || slot == 2U) ? (prg_mode ? 14U : r6) : r7;
        if (slot < 2U) {
            inner &= 0x0DU;
        } else {
            inner |= 0x02U;
        }
    } else {
        switch (slot) {
        case 0U:
            inner = prg_mode ? 14U : r6;
            break;
        case 1U:
            inner = r7;
            break;
        case 2U:
            inner = prg_mode ? r6 : 14U;
            break;
        default:
            inner = 15U;
            break;
        }
    }

    return static_cast<Byte>((inner & 0x0FU) | ((outer_bank_ & 0x06U) << 3U));
}

Byte Mapper315::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const std::size_t offset =
        static_cast<std::size_t>(prg_bank(addr)) * kPrgBankBytes + (addr & (kPrgBankBytes - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper315::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        // r7811 marks the $E000 decoder mask as probable, covering $6000-$7FFF.
        if ((addr & 0xE000U) == 0x6000U && outer_enabled_ && !outer_write_protected_) {
            outer_bank_ = value & 0x0FU;
        }
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & kBankSelectMask;
        break;
    case 0x8001U: {
        const unsigned index = bank_select_ & 7U;
        if (index < 2U) {
            bank_registers_[index] = value & 0xFEU;
        } else if (index >= 6U) {
            bank_registers_[index] = value & 0x3FU;
        } else {
            bank_registers_[index] = value;
        }
        break;
    }
    case 0xA000U:
        if (initial_mirror_ != MirrorMode::FourScreen) {
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        }
        break;
    case 0xA001U:
        outer_enabled_ = (value & 0x80U) != 0U;
        outer_write_protected_ = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        irq_latch_ = value;
        break;
    case 0xC001U:
        irq_counter_ = 0U;
        irq_reload_ = true;
        break;
    case 0xE000U:
        irq_enabled_ = false;
        irq_pending_ = false;
        break;
    case 0xE001U:
        irq_enabled_ = true;
        break;
    default:
        break;
    }
}

bool Mapper315::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper315::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : 0U;
}

unsigned Mapper315::chr_bank(Address addr) const {
    const unsigned slot = (addr >> 10U) & 7U;
    const bool inverted = (bank_select_ & 0x80U) != 0U;
    unsigned bank = 0U;

    if (!inverted) {
        switch (slot) {
        case 0U:
            bank = bank_registers_[0U] & 0xFEU;
            break;
        case 1U:
            bank = bank_registers_[0U] | 1U;
            break;
        case 2U:
            bank = bank_registers_[1U] & 0xFEU;
            break;
        case 3U:
            bank = bank_registers_[1U] | 1U;
            break;
        default:
            bank = bank_registers_[slot - 2U];
            break;
        }
    } else {
        if (slot < 4U) {
            bank = bank_registers_[slot + 2U];
        } else {
            const unsigned pair = slot - 4U;
            const Byte reg = bank_registers_[pair / 2U];
            bank = (pair & 1U) != 0U ? reg | 1U : reg & 0xFEU;
        }
    }

    if ((outer_bank_ & 0x08U) != 0U) {
        bank |= 0x40U;
    }
    if ((outer_bank_ & 0x04U) != 0U) {
        bank |= 0x80U;
    }
    if ((outer_bank_ & 0x01U) != 0U) {
        bank |= 0x100U;
    }
    return bank;
}

Byte Mapper315::read_chr(Address addr) {
    const std::size_t offset =
        static_cast<std::size_t>(chr_bank(addr)) * kChrBankBytes + (addr & (kChrBankBytes - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper315::write_chr(Address, Byte) {}

MirrorMode Mapper315::mirror_mode() const {
    return mirror_mode_;
}

void Mapper315::power_on() {
    reset();
}

void Mapper315::reset() {
    // Electrical startup is unspecified; these are deterministic emulator defaults.
    mirror_mode_ = initial_mirror_;
    bank_registers_.fill(0U);
    bank_select_ = 0U;
    outer_bank_ = 0U;
    irq_latch_ = 0U;
    irq_counter_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    outer_enabled_ = false;
    outer_write_protected_ = false;
}

bool Mapper315::irq_pending() const {
    return irq_pending_;
}

void Mapper315::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper315::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_) {
        irq_counter_ = irq_latch_;
    } else {
        --irq_counter_;
    }
    irq_reload_ = false;
    if (irq_counter_ == 0U && irq_enabled_) {
        irq_pending_ = true;
    }
}

void Mapper315::save_state(StateWriter& writer) const {
    Byte flags = 0U;
    flags |= irq_reload_ ? 0x01U : 0U;
    flags |= irq_enabled_ ? 0x02U : 0U;
    flags |= irq_pending_ ? 0x04U : 0U;
    flags |= outer_enabled_ ? 0x08U : 0U;
    flags |= outer_write_protected_ ? 0x10U : 0U;

    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(mirror_code(initial_mirror_));
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write(outer_bank_);
    writer.write(mirror_code(mirror_mode_));
    writer.write(irq_latch_);
    writer.write(irq_counter_);
    writer.write(flags);
}

void Mapper315::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> bank_registers{};
    reader.read_array(bank_registers);
    const auto outer_bank = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto flags = reader.read<Byte>();

    const bool invalid_banks = (bank_registers[0U] & 1U) != 0U || (bank_registers[1U] & 1U) != 0U ||
                               bank_registers[6U] > 0x3FU || bank_registers[7U] > 0x3FU;
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || initial_mirror != mirror_code(initial_mirror_) ||
        (bank_select & ~kBankSelectMask) != 0U || invalid_banks || outer_bank > 0x0FU ||
        mirror > 2U || (flags & ~kStateFlagsMask) != 0U) {
        reader.fail();
        return;
    }

    bank_select_ = bank_select;
    bank_registers_ = bank_registers;
    outer_bank_ = outer_bank;
    mirror_mode_ = decode_mirror(mirror);
    irq_latch_ = irq_latch;
    irq_counter_ = irq_counter;
    irq_reload_ = (flags & 0x01U) != 0U;
    irq_enabled_ = (flags & 0x02U) != 0U;
    irq_pending_ = (flags & 0x04U) != 0U;
    outer_enabled_ = (flags & 0x08U) != 0U;
    outer_write_protected_ = (flags & 0x10U) != 0U;
}

} // namespace mapperbus::core
