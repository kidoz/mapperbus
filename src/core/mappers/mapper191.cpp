#include "core/mappers/mapper191.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Mapper191::Mapper191(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      outer_enabled_(!header.is_nes2 ? chr_rom_.size() == 262144U : header.submapper == 1U),
      initial_mirror_(header.mirror_mode), mirror_mode_(initial_mirror_) {
    power_on();
}

Result<void> Mapper191::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 191U || (header.is_nes2 && header.submapper > 1U))
        return std::unexpected("Mapper 191 supports only mapper 191 submappers 0 and 1");
    if (prg_size != 262144U || (chr_size != 131072U && chr_size != 262144U))
        return std::unexpected("Mapper 191 requires 256 KiB PRG and 128 or 256 KiB CHR ROM");
    if (header.is_nes2 && header.submapper == 1U && chr_size != 262144U)
        return std::unexpected("Mapper 191 submapper 1 requires the full 256 KiB CHR ROM");
    if (header.has_battery || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 2048U)))
        return std::unexpected("Mapper 191 requires only 2 KiB volatile CHR RAM and no battery");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 191 requires horizontal or vertical CIRAM mirroring");
    return {};
}

unsigned Mapper191::effective_outer_bank() const {
    return outer_enabled_ ? outer_bank_ : 3U;
}

Byte Mapper191::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;

    const unsigned slot = (addr - 0x8000U) / 8192U;
    const unsigned fixed_second_last = 0x18U + effective_outer_bank() * 2U;
    unsigned bank = bank_registers_[7U];
    if (slot == 3U)
        bank = fixed_second_last + 1U;
    else if (slot == (prg_mode_ ? 0U : 2U))
        bank = fixed_second_last;
    else if (slot == 0U || slot == 2U)
        bank = bank_registers_[6U];

    return prg_rom_[(bank * 8192U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void Mapper191::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;

    if (outer_enabled_ && (addr & 0xF0FFU) == 0x90AAU)
        outer_bank_ = value & 3U;

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 7U;
        prg_mode_ = (value & 0x40U) != 0U;
        chr_inversion_ = (value & 0x80U) != 0U;
        break;
    case 0x8001U:
        bank_registers_[bank_select_] = bank_select_ >= 6U ? value & 0x3FU : value;
        break;
    case 0xA000U:
        mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
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

Byte Mapper191::chr_bank(Address addr) const {
    const unsigned slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    if (slot < 4U)
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    return bank_registers_[slot - 2U];
}

Byte Mapper191::read_chr(Address addr) {
    const unsigned bank = chr_bank(addr);
    if (effective_outer_bank() == 3U && (bank & 0x80U) != 0U)
        return chr_ram_[(bank & 1U) * 1024U + (addr & 0x3FFU)];

    const unsigned rom_bank = effective_outer_bank() == 3U ? bank : (bank & 0x7FU) | 0x80U;
    return chr_rom_[rom_bank * 1024U + (addr & 0x3FFU)];
}

void Mapper191::write_chr(Address addr, Byte value) {
    const unsigned bank = chr_bank(addr);
    if (effective_outer_bank() == 3U && (bank & 0x80U) != 0U)
        chr_ram_[(bank & 1U) * 1024U + (addr & 0x3FFU)] = value;
}

MirrorMode Mapper191::mirror_mode() const {
    return mirror_mode_;
}

void Mapper191::power_on() {
    chr_ram_.fill(0U);
    reset();
}

void Mapper191::reset() {
    outer_bank_ = 0U;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    mirror_mode_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

void Mapper191::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_) {
        irq_counter_ = irq_latch_;
        irq_reload_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

uint16_t Mapper191::state_tag() const {
    return static_cast<uint16_t>(0xBF00U | (outer_enabled_ ? 1U : 0U) |
                                 (chr_rom_.size() == 262144U ? 2U : 0U) |
                                 (initial_mirror_ == MirrorMode::Vertical ? 4U : 0U));
}

void Mapper191::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(outer_bank_);
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write_array(chr_ram_);
}

void Mapper191::load_state(StateReader& reader) {
    struct State {
        Byte outer = 0U;
        Byte select = 0U;
        Byte mode = 0U;
        std::array<Byte, 8> banks{};
        Byte mirror = 0U;
        Byte counter = 0U;
        Byte latch = 0U;
        Byte irq = 0U;
        std::array<Byte, 2048> chr_ram{};
    } next;

    const auto tag = reader.read<uint16_t>();
    reader.read(next.outer);
    reader.read(next.select);
    reader.read(next.mode);
    reader.read_array(next.banks);
    reader.read(next.mirror);
    reader.read(next.counter);
    reader.read(next.latch);
    reader.read(next.irq);
    reader.read_array(next.chr_ram);

    if (!reader.ok() || tag != state_tag() || next.outer > 3U ||
        (!outer_enabled_ && next.outer != 0U) || next.select > 7U || (next.mode & 0x3FU) != 0U ||
        next.banks[6U] > 0x3FU || next.banks[7U] > 0x3FU ||
        next.mirror > static_cast<Byte>(MirrorMode::Vertical) || next.irq > 7U ||
        ((next.irq & 4U) != 0U && (next.irq & 2U) == 0U)) {
        reader.fail();
        return;
    }

    outer_bank_ = next.outer;
    bank_select_ = next.select;
    prg_mode_ = (next.mode & 0x40U) != 0U;
    chr_inversion_ = (next.mode & 0x80U) != 0U;
    bank_registers_ = next.banks;
    mirror_mode_ = static_cast<MirrorMode>(next.mirror);
    irq_counter_ = next.counter;
    irq_latch_ = next.latch;
    irq_reload_ = (next.irq & 1U) != 0U;
    irq_enabled_ = (next.irq & 2U) != 0U;
    irq_pending_ = (next.irq & 4U) != 0U;
    chr_ram_ = next.chr_ram;
}

} // namespace mapperbus::core
