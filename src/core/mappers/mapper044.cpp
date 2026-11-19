#include "core/mappers/mapper044.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

struct BlockPins {
    unsigned prg_and;
    unsigned prg_or;
    unsigned chr_and;
    unsigned chr_or;
};

constexpr std::array<BlockPins, 8> kBlockPins = {{
    {0x0FU, 0x00U, 0x7FU, 0x000U},
    {0x0FU, 0x10U, 0x7FU, 0x080U},
    {0x0FU, 0x20U, 0x7FU, 0x100U},
    {0x0FU, 0x30U, 0x7FU, 0x180U},
    {0x0FU, 0x40U, 0x7FU, 0x200U},
    {0x0FU, 0x50U, 0x7FU, 0x280U},
    {0x1FU, 0x60U, 0xFFU, 0x300U},
    {0x1FU, 0x60U, 0xFFU, 0x300U},
}};

} // namespace

Mapper044::Mapper044(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_(header.is_nes2 ? (header.prg_ram_size + header.prg_nvram_size) : 0x2000U),
      initial_mirror_(header.mirror_mode), mirror_mode_(header.mirror_mode),
      persistent_ram_(header.has_battery) {
    power_on();
}

Result<void> Mapper044::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 044 supports submapper 0");
    if (prg_size != 0x100000U || chr_size != 0x100000U)
        return std::unexpected("Mapper 044 requires 1 MiB PRG and 1 MiB CHR ROM");
    if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 044 does not support CHR RAM or NVRAM");
    if (header.is_nes2) {
        const bool valid_ram =
            header.has_battery ? header.prg_ram_size == 0U && header.prg_nvram_size == 0x2000U
                               : header.prg_nvram_size == 0U &&
                                     (header.prg_ram_size == 0U || header.prg_ram_size == 0x2000U);
        if (!valid_ram)
            return std::unexpected(
                "Mapper 044 requires absent or 8 KiB PRG RAM matching the battery flag");
    }
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 044 supports ordinary H/V CIRAM");
    return {};
}

unsigned Mapper044::prg_bank(unsigned slot) const {
    const auto& pins = kBlockPins[block_];
    const bool large_block = block_ >= 6U;
    const unsigned second_last = large_block ? 30U : 14U;
    const unsigned last = large_block ? 31U : 15U;
    unsigned inner = 0U;
    switch (slot) {
    case 0U:
        inner = prg_mode_ ? second_last : bank_registers_[6U];
        break;
    case 1U:
        inner = bank_registers_[7U];
        break;
    case 2U:
        inner = prg_mode_ ? bank_registers_[6U] : second_last;
        break;
    default:
        inner = last;
        break;
    }
    return (inner & pins.prg_and) | pins.prg_or;
}

unsigned Mapper044::chr_bank(Address addr) const {
    const auto slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    const unsigned inner =
        slot < 4U ? (bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U) : bank_registers_[slot - 2U];
    const auto& pins = kBlockPins[block_];
    return (inner & pins.chr_and) | pins.chr_or;
}

Byte Mapper044::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return prg_rom_[prg_bank((addr - 0x8000U) / 0x2000U) * 0x2000U + (addr & 0x1FFFU)];
    if (addr >= 0x6000U && prg_ram_enabled_ && !prg_ram_.empty())
        return prg_ram_[addr & 0x1FFFU];
    return 0U;
}

void Mapper044::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_ && !prg_ram_.empty())
            prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 7U;
        prg_mode_ = (value & 0x40U) != 0U;
        chr_inversion_ = (value & 0x80U) != 0U;
        break;
    case 0x8001U:
        bank_registers_[bank_select_] =
            bank_select_ >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        break;
    case 0xA000U:
        mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        prg_ram_enabled_ = (value & 0x80U) != 0U;
        prg_ram_write_protected_ = (value & 0x40U) != 0U;
        block_ = value & 7U;
        break;
    case 0xC000U:
        irq_reload_ = value;
        break;
    case 0xC001U:
        irq_counter_ = 0U;
        irq_reload_flag_ = true;
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

bool Mapper044::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && prg_ram_enabled_ && !prg_ram_.empty());
}

Byte Mapper044::read_chr(Address addr) {
    return chr_rom_[chr_bank(addr) * 0x400U + (addr & 0x3FFU)];
}

void Mapper044::write_chr(Address, Byte) {}

MirrorMode Mapper044::mirror_mode() const {
    return mirror_mode_;
}

void Mapper044::power_on() {
    reset();
    if (!persistent_ram_)
        std::ranges::fill(prg_ram_, 0U);
}

void Mapper044::reset() {
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    block_ = 0U;
    mirror_mode_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
}

bool Mapper044::irq_pending() const {
    return irq_pending_;
}

void Mapper044::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper044::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

std::span<const Byte> Mapper044::battery_ram() const {
    return persistent_ram_ ? std::span<const Byte>(prg_ram_) : std::span<const Byte>{};
}

void Mapper044::set_battery_ram(std::span<const Byte> data) {
    if (persistent_ram_ && data.size() == prg_ram_.size())
        std::ranges::copy(data, prg_ram_.begin());
}

uint16_t Mapper044::state_tag() const {
    return static_cast<uint16_t>(0x2C00U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                 (persistent_ram_ ? 2U : 0U) | (!prg_ram_.empty() ? 4U : 0U));
}

void Mapper044::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write_bytes(prg_ram_);
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write<Byte>(block_ | (prg_ram_enabled_ ? 0x80U : 0U) |
                       (prg_ram_write_protected_ ? 0x40U : 0U));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
}

void Mapper044::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::vector<Byte> ram;
    reader.read_bytes(ram, 0x2000U);
    const auto select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> registers{};
    reader.read_array(registers);
    const auto mirror = reader.read<Byte>();
    const auto ram_and_block = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || ram.size() != prg_ram_.size() || select > 7U ||
        (mode & 0x3FU) != 0U || registers[6U] > 0x3FU || registers[7U] > 0x3FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || (ram_and_block & 0x38U) != 0U ||
        irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U)) {
        reader.fail();
        return;
    }

    prg_ram_ = std::move(ram);
    bank_select_ = select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = registers;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    block_ = ram_and_block & 7U;
    prg_ram_enabled_ = (ram_and_block & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram_and_block & 0x40U) != 0U;
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
}

} // namespace mapperbus::core
