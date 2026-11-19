#include "core/mappers/mapper291.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomBytes = 256U * 1024U;
constexpr std::size_t kChrRomBytes = 512U * 1024U;
constexpr std::size_t kPrgOuterBytes = 128U * 1024U;
constexpr std::size_t kChrOuterBytes = 256U * 1024U;
constexpr std::size_t kPrgInnerBytes = 32U * 1024U;
constexpr std::size_t kPrgSlotBytes = 8U * 1024U;
constexpr std::size_t kChrSlotBytes = 1024U;

} // namespace

// Original implementation from NESdev mapper 291 r7746 and its MMC3 reference.
Mapper291::Mapper291(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(header.mirror_mode) {}

Result<void> Mapper291::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 291U || header.submapper != 0U)
        return std::unexpected("Mapper 291 requires NES 2.0 submapper 0");
    if (prg_size != kPrgRomBytes || chr_size != kChrRomBytes)
        return std::unexpected("Mapper 291 requires 256 KiB PRG ROM and 512 KiB CHR ROM");
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 291 supports the documented ROM-only population");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 291 requires H/V nametable wiring");
    return {};
}

std::size_t Mapper291::prg_outer_offset() const {
    return (outer_bank_ & 0x40U) != 0U ? kPrgOuterBytes : 0U;
}

std::size_t Mapper291::chr_outer_offset() const {
    return (outer_bank_ & 0x40U) != 0U ? kChrOuterBytes : 0U;
}

Byte Mapper291::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;

    if ((outer_bank_ & 0x20U) != 0U) {
        const std::size_t inner =
            static_cast<std::size_t>((outer_bank_ >> 1U) & 3U) * kPrgInnerBytes;
        return prg_rom_[prg_outer_offset() + inner + (addr & 0x7FFFU)];
    }

    const unsigned slot = (addr - 0x8000U) / kPrgSlotBytes;
    unsigned bank = 0U;
    if (slot == 1U) {
        bank = bank_registers_[7U] & 0x0FU;
    } else if (slot == 3U) {
        bank = 15U;
    } else if ((slot == 2U) == ((bank_select_ & 0x40U) != 0U)) {
        bank = bank_registers_[6U] & 0x0FU;
    } else {
        bank = 14U;
    }
    return prg_rom_[prg_outer_offset() + bank * kPrgSlotBytes + (addr & 0x1FFFU)];
}

void Mapper291::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000U) == 0x6000U) {
        // Bit 7 has no documented function; retain it without assigning behavior.
        outer_bank_ = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 0xC7U;
        break;
    case 0x8001U: {
        const unsigned index = bank_select_ & 7U;
        bank_registers_[index] = index >= 6U ? value & 0x3FU : value;
        break;
    }
    case 0xA000U:
        mirror_ = (value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        break;
    case 0xA001U:
        ram_control_ = value & 0xC0U;
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

Byte Mapper291::read_chr(Address addr) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    const unsigned bank = slot < 4U ? (bank_registers_[slot >> 1U] & 0xFEU) | (slot & 1U)
                                    : bank_registers_[slot - 2U];
    return chr_rom_[chr_outer_offset() + bank * kChrSlotBytes + (addr & 0x03FFU)];
}

void Mapper291::write_chr(Address, Byte) {}

MirrorMode Mapper291::mirror_mode() const {
    return mirror_;
}

void Mapper291::reset() {
    // Electrical reset values are unspecified; use deterministic emulator defaults.
    outer_bank_ = 0U;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    ram_control_ = 0U;
    mirror_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

bool Mapper291::irq_pending() const {
    return irq_pending_;
}

void Mapper291::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper291::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_) {
        irq_counter_ = irq_latch_;
        irq_reload_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

std::uint16_t Mapper291::state_tag() const {
    return static_cast<std::uint16_t>(0x1230U |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper291::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(outer_bank_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write(static_cast<Byte>(mirror_));
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write(ram_control_);
}

void Mapper291::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer = reader.read<Byte>();
    const auto select = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto latch = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram_control = reader.read<Byte>();

    const bool valid_banks = banks[6U] <= 0x3FU && banks[7U] <= 0x3FU;
    const bool valid_irq = irq <= 7U && ((irq & 4U) == 0U || (irq & 2U) != 0U);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (select & 0x38U) != 0U || !valid_banks ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || !valid_irq ||
        (ram_control & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer;
    bank_select_ = select;
    bank_registers_ = banks;
    mirror_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_latch_ = latch;
    irq_reload_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    ram_control_ = ram_control;
}

} // namespace mapperbus::core
