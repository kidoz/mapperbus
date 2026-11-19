#include "core/mappers/mapper292.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPrgBankSize = 8192U;
constexpr std::size_t kChrSize = 512U * 1024U;
} // namespace

// Original implementation from NESdev Mapper 292 r22336. Electrical reset is
// unspecified; reset uses deterministic zeroed latches and the core's MMC3 IRQ policy.
Mapper292::Mapper292(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper292::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.submapper != 0U)
        return std::unexpected("Mapper 292 requires NES 2.0 submapper 0");
    if (prg_size < 32768U || prg_size > 512U * 1024U || prg_size % kPrgBankSize != 0U)
        return std::unexpected("Mapper 292 requires 32-512 KiB PRG ROM in 8 KiB units");
    if (chr_size != kChrSize)
        return std::unexpected("Mapper 292 requires exactly 512 KiB CHR ROM");
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 292 exposes no RAM or persistent memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 292 requires H/V nametable wiring");
    return {};
}

bool Mapper292::maps_prg(Address addr) const {
    return addr >= 0x8000U || ((addr & 0xE000U) == 0x6000U && ram_enabled_);
}

Byte Mapper292::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : 0U;
}

std::size_t Mapper292::prg_bank(Address addr) const {
    const std::size_t count = prg_rom_.size() / kPrgBankSize;
    const bool prg_mode = (bank_select_ & 0x40U) != 0U;
    const unsigned slot = (addr - 0x8000U) >> 13U;
    if (slot == 1U)
        return bank_registers_[7U] % count;
    if (slot == 3U)
        return count - 1U;
    if ((slot == 2U) == prg_mode)
        return bank_registers_[6U] % count;
    return count - 2U;
}

Byte Mapper292::read_prg(Address addr) {
    if ((addr & 0xE000U) == 0x6000U) {
        if (ram_enabled_)
            extra_registers_[extra_index_ & 1U] = last_write_;
        return 0U;
    }
    if (addr < 0x8000U)
        return 0U;
    return prg_rom_[prg_bank(addr) * kPrgBankSize + (addr & 0x1FFFU)];
}

void Mapper292::observe_cpu_write(Address, Byte value) {
    last_write_ = value;
}

void Mapper292::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000U) == 0x6000U) {
        if (ram_enabled_)
            extra_index_ = value;
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
        bank_registers_[index] = index < 2U ? value & 0xFEU : index < 6U ? value : value & 0x3FU;
        break;
    }
    case 0xA000U:
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        ram_enabled_ = (value & 0x80U) != 0U;
        ram_write_protected_ = (value & 0x40U) != 0U;
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

std::size_t Mapper292::chr_offset(Address addr) const {
    const bool inverted = (bank_select_ & 0x80U) != 0U;
    if (addr < 0x1000U) {
        const bool second_region = addr >= 0x0800U;
        const bool upper_half = (addr & 0x0400U) != 0U;
        unsigned index = 0U;
        if (inverted)
            index = second_region ? 4U + upper_half : 2U + upper_half;
        else
            index = second_region ? 1U : 0U;

        unsigned bank = bank_registers_[index] >> 1U;
        if (second_region)
            bank ^= (static_cast<unsigned>(extra_registers_[1U]) << 1U) & 0x80U;
        else
            bank ^= extra_registers_[0U];
        return bank * 2048U + (addr & 0x07FFU);
    }
    return static_cast<std::size_t>(extra_registers_[1U] & 0x3FU) * 4096U + (addr & 0x0FFFU);
}

Byte Mapper292::read_chr(Address addr) {
    return chr_rom_[chr_offset(addr & 0x1FFFU)];
}

void Mapper292::write_chr(Address, Byte) {}

MirrorMode Mapper292::mirror_mode() const {
    return mirror_;
}

void Mapper292::reset() {
    mirror_ = initial_mirror_;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    extra_index_ = 0U;
    extra_registers_.fill(0U);
    last_write_ = 0U;
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    ram_enabled_ = true;
    ram_write_protected_ = false;
}

bool Mapper292::irq_pending() const {
    return irq_pending_;
}

void Mapper292::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper292::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_) {
        irq_counter_ = irq_latch_;
        irq_reload_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

std::uint16_t Mapper292::state_tag() const {
    return static_cast<std::uint16_t>(0x1240U |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper292::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write(static_cast<Byte>(mirror_));
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((ram_enabled_ ? 0x80U : 0U) | (ram_write_protected_ ? 0x40U : 0U));
    writer.write(extra_index_);
    writer.write_array(extra_registers_);
    writer.write(last_write_);
}

void Mapper292::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto select = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto latch = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    const auto index = reader.read<Byte>();
    std::array<Byte, 2> extra{};
    reader.read_array(extra);
    const auto last_write = reader.read<Byte>();

    const bool valid_banks = (banks[0U] & 1U) == 0U && (banks[1U] & 1U) == 0U &&
                             banks[6U] <= 0x3FU && banks[7U] <= 0x3FU;
    const bool valid_irq = irq <= 7U && ((irq & 4U) == 0U || (irq & 2U) != 0U);
    if (!reader.ok() || reader.remaining() != 0U || tag != state_tag() ||
        prg_size != prg_rom_.size() || chr_size != chr_rom_.size() || (select & 0x38U) != 0U ||
        !valid_banks || mirror > static_cast<Byte>(MirrorMode::Vertical) || !valid_irq ||
        (ram & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    bank_select_ = select;
    bank_registers_ = banks;
    mirror_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_latch_ = latch;
    irq_reload_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    ram_enabled_ = (ram & 0x80U) != 0U;
    ram_write_protected_ = (ram & 0x40U) != 0U;
    extra_index_ = index;
    extra_registers_ = extra;
    last_write_ = last_write;
}

} // namespace mapperbus::core
