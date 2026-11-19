#include "core/mappers/mapper516.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x080000UZ;
constexpr std::size_t kInnerBankSize = 0x020000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x5160U;
constexpr Address kOuterRegisterMask = 0x8010U;
constexpr Address kOuterRegisterValue = 0x8010U;
constexpr Address kMmc3RegisterMask = 0xE011U;
constexpr Byte kOuterBankMask = 0x0FU;
constexpr Byte kBankSelectMask = 0xC7U;
constexpr Byte kIrqReloadFlag = 0x01U;
constexpr Byte kIrqEnabledFlag = 0x02U;
constexpr Byte kIrqPendingFlag = 0x04U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 516 oldid 8088.
// Reset is unspecified: clear all latches and restore the header's CIRAM arrangement.
Mapper516::Mapper516(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_mode_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper516::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 516U) {
        return std::unexpected("Mapper 516 requires mapper ID 516");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 516 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxRomSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxRomSize) {
        return std::unexpected(
            "Mapper 516 requires power-of-two 32-512 KiB PRG and 8-512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 516 requires ROM-only cartridge memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 516 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 516 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper516::prg_offset(Address addr) const {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    const bool second_fixed = (slot == 2U && (bank_select_ & 0x40U) == 0U) ||
                              (slot == 0U && (bank_select_ & 0x40U) != 0U);
    const bool r6_selected = (slot == 0U && (bank_select_ & 0x40U) == 0U) ||
                             (slot == 2U && (bank_select_ & 0x40U) != 0U);
    const bool r7_selected = slot == 1U;
    unsigned inner = 0x0FU;
    if (second_fixed) {
        inner = 0x0EU;
    } else if (r6_selected) {
        inner = bank_registers_[6U] & 0x0FU;
    } else if (r7_selected) {
        inner = bank_registers_[7U] & 0x0FU;
    }
    const auto outer = static_cast<std::size_t>(outer_bank_ & 3U);
    return outer * kInnerBankSize + inner * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper516::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper516::write_prg(Address addr, Byte value) {
    if ((addr & kOuterRegisterMask) == kOuterRegisterValue) {
        outer_bank_ = value & kOuterBankMask;
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & kMmc3RegisterMask) {
    case 0x8000U:
        bank_select_ = value & kBankSelectMask;
        break;
    case 0x8001U:
        bank_registers_[bank_select_ & 7U] = value;
        break;
    case 0xA000U:
        mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
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

bool Mapper516::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper516::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper516::chr_offset(Address addr) const {
    const auto slot =
        static_cast<unsigned>(addr / kChrBankSize) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    unsigned inner = 0U;
    if (slot < 4U) {
        inner = (bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U);
    } else {
        inner = bank_registers_[slot - 2U];
    }
    inner &= 0x7FU;
    const auto outer = static_cast<std::size_t>((outer_bank_ >> 2U) & 3U);
    return outer * kInnerBankSize + inner * kChrBankSize + (addr & (kChrBankSize - 1U));
}

Byte Mapper516::read_chr(Address addr) {
    if (addr >= 0x2000U || chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper516::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper516::mirror_mode() const {
    return mirror_mode_;
}

void Mapper516::power_on() {
    reset();
}

void Mapper516::reset() {
    mirror_mode_ = initial_mirror_;
    outer_bank_ = 0U;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    irq_latch_ = 0U;
    irq_counter_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

bool Mapper516::irq_pending() const {
    return irq_pending_;
}

void Mapper516::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper516::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_) {
        irq_counter_ = irq_latch_;
        irq_reload_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_) {
        irq_pending_ = true;
    }
}

std::uint16_t Mapper516::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper516::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(outer_bank_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(mirror_mode_ == MirrorMode::Horizontal ? 1U : 0U);
    writer.write(irq_latch_);
    writer.write(irq_counter_);
    writer.write<Byte>((irq_reload_ ? kIrqReloadFlag : 0U) | (irq_enabled_ ? kIrqEnabledFlag : 0U) |
                       (irq_pending_ ? kIrqPendingFlag : 0U));
}

void Mapper516::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer_bank = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> bank_registers{};
    reader.read_array(bank_registers);
    const auto mirror = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (outer_bank & ~kOuterBankMask) != 0U ||
        (bank_select & ~kBankSelectMask) != 0U || mirror > 1U || irq_flags > 7U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer_bank;
    bank_select_ = bank_select;
    bank_registers_ = bank_registers;
    mirror_mode_ = mirror == 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    irq_latch_ = irq_latch;
    irq_counter_ = irq_counter;
    irq_reload_ = (irq_flags & kIrqReloadFlag) != 0U;
    irq_enabled_ = (irq_flags & kIrqEnabledFlag) != 0U;
    irq_pending_ = (irq_flags & kIrqPendingFlag) != 0U;
}

} // namespace mapperbus::core
