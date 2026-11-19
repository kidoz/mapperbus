#include "core/mappers/mapper049.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev INES Mapper 049 r4300. The outer register is gated by the MMC3 RAM
// controls even though this board has no RAM. Decode block pins before wrapping
// the fitted ROM, and retain the project's Sharp-style repeating-zero IRQ model.
Mapper049::Mapper049(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_mode_(header.mirror_mode) {
    reset();
}

Result<void> Mapper049::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 049 supports submapper 0");
    if (prg_size < 32768U || prg_size > 524288U || prg_size % 32768U != 0U || chr_size < 8192U ||
        chr_size > 524288U || chr_size % 1024U != 0U)
        return std::unexpected("Mapper 049 requires 32-512 KiB PRG and 8-512 KiB CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 049 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 049 supports ordinary H/V CIRAM");
    return {};
}

Byte Mapper049::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;

    const unsigned block = (outer_ >> 6U) & 3U;
    unsigned offset = 0U;
    if ((outer_ & 1U) == 0U) {
        offset = block * 0x20000U + ((outer_ >> 4U) & 3U) * 0x8000U + (addr & 0x7FFFU);
    } else {
        const unsigned slot = (addr - 0x8000U) / 0x2000U;
        const unsigned r6 = bank_registers_[6U] & 0x0FU;
        const unsigned r7 = bank_registers_[7U] & 0x0FU;
        const unsigned bank = slot == 1U                   ? r7
                              : slot == 3U                 ? 15U
                              : (slot == 0U) == !prg_mode_ ? r6
                                                           : 14U;
        offset = block * 0x20000U + bank * 0x2000U + (addr & 0x1FFFU);
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper049::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_)
            outer_ = value & 0xF1U;
        return;
    }
    if (addr < 0x8000U)
        return;

    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            bank_select_ = value & 7U;
            prg_mode_ = (value & 0x40U) != 0U;
            chr_inversion_ = (value & 0x80U) != 0U;
        } else {
            bank_registers_[bank_select_] = bank_select_ >= 6U ? value & 0x3FU : value;
        }
    } else if (addr < 0xC000U) {
        if (even)
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        else {
            prg_ram_enabled_ = (value & 0x80U) != 0U;
            prg_ram_write_protected_ = (value & 0x40U) != 0U;
        }
    } else if (addr < 0xE000U) {
        if (even)
            irq_reload_ = value;
        else {
            irq_counter_ = 0U;
            irq_reload_flag_ = true;
        }
    } else if (even) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

bool Mapper049::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper049::prg_read_mask(Address addr) const {
    return addr >= 0x6000U && addr < 0x8000U ? 0U : 0xFFU;
}

Byte Mapper049::read_chr(Address addr) {
    const auto slot = static_cast<unsigned>(((addr >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U));
    const unsigned bank =
        slot < 4U ? (bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U) : bank_registers_[slot - 2U];
    const unsigned offset = ((outer_ >> 6U) & 3U) * 0x20000U + bank * 0x400U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper049::write_chr(Address, Byte) {}

MirrorMode Mapper049::mirror_mode() const {
    return mirror_mode_;
}

void Mapper049::reset() {
    outer_ = 0U;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    mirror_mode_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
}

void Mapper049::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

uint16_t Mapper049::state_tag() const {
    return static_cast<uint16_t>(0x3100U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper049::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(outer_);
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
}

void Mapper049::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || (outer & 0x0EU) != 0U || bank_select > 7U ||
        (mode & 0x3FU) != 0U || banks[6U] > 0x3FU || banks[7U] > 0x3FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    outer_ = outer;
    bank_select_ = bank_select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = irq_counter;
    irq_reload_ = irq_reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    prg_ram_enabled_ = (ram & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram & 0x40U) != 0U;
}

} // namespace mapperbus::core
