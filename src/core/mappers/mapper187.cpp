#include "core/mappers/mapper187.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Mapper187::Mapper187(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper187::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 187U || (header.is_nes2 && header.submapper != 0U))
        return std::unexpected("Mapper 187 supports only mapper 187 submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U)
        return std::unexpected("Mapper 187 requires 32-512 KiB PRG ROM in 8 KiB units");
    if (chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
        return std::unexpected("Mapper 187 requires 8-512 KiB CHR ROM in 1 KiB units");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 187 supported boards contain PRG/CHR ROM without RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 187 supports ordinary H/V CIRAM");
    return {};
}

void Mapper187::reset() {
    mirror_mode_ = initial_mirror_;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    prg_ram_control_ = 0x80U;
    nrom_override_ = 0U;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

Byte Mapper187::selected_prg_bank(Address addr) const {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / 0x2000U);
    const std::size_t bank_count = prg_rom_.size() / 0x2000U;
    if (slot == 1U)
        return static_cast<Byte>((bank_registers_[7U] & 0x3FU) % bank_count);
    if (slot == 3U)
        return static_cast<Byte>(bank_count - 1U);
    const bool fixed = (slot == 0U) == prg_mode_;
    return fixed ? static_cast<Byte>(bank_count - 2U)
                 : static_cast<Byte>((bank_registers_[6U] & 0x3FU) % bank_count);
}

Byte Mapper187::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty())
        return 0U;
    std::size_t offset = 0U;
    if ((nrom_override_ & 0x80U) != 0U) {
        unsigned bank = nrom_override_ & 0x1FU;
        if ((nrom_override_ & 0x20U) != 0U)
            bank = (bank & 0x1EU) | ((addr >> 14U) & 1U);
        offset = bank * 0x4000U + (addr & 0x3FFFU);
    } else {
        offset = static_cast<std::size_t>(selected_prg_bank(addr)) * 0x2000U + (addr & 0x1FFFU);
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper187::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    if (addr < 0xA000U) {
        if ((addr & 1U) == 0U) {
            bank_select_ = value & 7U;
            prg_mode_ = (value & 0x40U) != 0U;
            chr_inversion_ = (value & 0x80U) != 0U;
        } else {
            bank_registers_[bank_select_] = bank_select_ >= 6U ? value & 0x3FU : value;
        }
    } else if (addr < 0xC000U) {
        if ((addr & 1U) == 0U)
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        else
            prg_ram_control_ = value & 0xC0U;
    } else if (addr < 0xE000U) {
        if ((addr & 1U) == 0U)
            irq_reload_ = value;
        else {
            irq_counter_ = 0U;
            irq_reload_flag_ = true;
        }
    } else if ((addr & 1U) == 0U) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

Byte Mapper187::selected_chr_bank(Address addr) const {
    const auto slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    if (slot < 4U)
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    return bank_registers_[slot - 2U];
}

Byte Mapper187::read_chr(Address addr) {
    if (chr_rom_.empty())
        return 0U;
    const unsigned bank =
        static_cast<unsigned>(selected_chr_bank(addr)) | ((addr & 0x1000U) != 0U ? 0x100U : 0U);
    const std::size_t offset = bank * 0x400U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper187::write_chr(Address, Byte) {}

MirrorMode Mapper187::mirror_mode() const {
    return mirror_mode_;
}

bool Mapper187::irq_pending() const {
    return irq_pending_;
}

void Mapper187::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper187::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

bool Mapper187::maps_expansion(Address addr) const {
    return (addr & 0xF000U) == 0x5000U;
}

Byte Mapper187::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x80U : 0U;
}

Byte Mapper187::read_expansion(Address addr) {
    return maps_expansion(addr) ? 0x80U : 0U;
}

void Mapper187::write_expansion(Address addr, Byte value) {
    if ((addr & 0xF001U) == 0x5000U)
        nrom_override_ = value & 0xBFU;
}

uint16_t Mapper187::state_tag() const {
    return static_cast<uint16_t>(0xBB00U | (initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U));
}

void Mapper187::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write(prg_ram_control_);
    writer.write(nrom_override_);
}

void Mapper187::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto bank_select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram_control = reader.read<Byte>();
    const auto nrom_override = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || bank_select > 7U || (mode & 0x3FU) != 0U ||
        banks[6U] > 0x3FU || banks[7U] > 0x3FU ||
        (mirror != static_cast<Byte>(MirrorMode::Horizontal) &&
         mirror != static_cast<Byte>(MirrorMode::Vertical)) ||
        irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram_control & 0x3FU) != 0U ||
        (nrom_override & 0x40U) != 0U) {
        reader.fail();
        return;
    }

    bank_select_ = bank_select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    prg_ram_control_ = ram_control;
    nrom_override_ = nrom_override;
}

} // namespace mapperbus::core
