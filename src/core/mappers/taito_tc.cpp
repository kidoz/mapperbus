#include "core/mappers/taito_tc.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

// Banking: NESdev mapper 33 r24359 and mapper 48 r22585.
// TC0690 measured latch/count behavior: https://forums.nesdev.org/viewtopic.php?t=18277
TaitoTc::TaitoTc(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), has_irq_(header.mapper_number == 48U),
      next_edge_reload_(has_irq_ && header.is_nes2 && header.submapper == 1U),
      irq_delay_cycles_(next_edge_reload_ ? 6U : 22U),
      initial_mirror_(header.mirror_mode == MirrorMode::Horizontal ? 1U : 0U),
      chr_ram_size_(header.chr_ram_size != 0U ? header.chr_ram_size : 0x2000U) {
    reset();
}

void TaitoTc::reset() {
    prg_banks_ = {0U, 1U};
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    irq_reload_ = 0U;
    irq_counter_ = 0U;
    irq_reload_pending_ = 0U;
    irq_enabled_ = 0U;
    irq_latched_ = 0U;
    irq_delay_ = 0U;
}

Byte TaitoTc::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto slot = (addr - 0x8000U) / 0x2000U;
    const std::size_t bank = slot < 2U ? prg_banks_[slot] : prg_rom_.size() / 0x2000U - 4U + slot;
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void TaitoTc::write_prg(Address address, Byte value) {
    const auto addr = static_cast<unsigned>(address);
    if (addr < 0x8000U || (!has_irq_ && addr >= 0xC000U))
        return;
    const auto reg = addr & (has_irq_ ? 0xE003U : 0xA003U);
    switch (reg) {
    case 0x8000U:
        prg_banks_[0U] = value & 0x3FU;
        if (!has_irq_)
            mirror_ = (value & 0x40U) != 0U ? 1U : 0U;
        break;
    case 0x8001U:
        prg_banks_[1U] = value & 0x3FU;
        break;
    case 0x8002U:
    case 0x8003U:
        chr_banks_[reg & 1U] = value;
        break;
    case 0xA000U:
    case 0xA001U:
    case 0xA002U:
    case 0xA003U:
        chr_banks_[2U + (reg & 3U)] = value;
        break;
    case 0xC000U:
        irq_reload_ = value;
        break;
    case 0xC001U:
        irq_latched_ = 0U;
        irq_delay_ = 0U;
        irq_counter_ = irq_reload_;
        irq_reload_pending_ = next_edge_reload_ ? 1U : 0U;
        break;
    case 0xC002U:
        irq_enabled_ = 1U;
        break;
    case 0xC003U:
        irq_enabled_ = 0U;
        break;
    case 0xE000U:
        mirror_ = (value & 0x40U) != 0U ? 1U : 0U;
        break;
    default:
        break;
    }
}

std::size_t TaitoTc::chr_offset(Address address) const {
    const auto addr = static_cast<unsigned>(address) & 0x1FFFU;
    if (addr < 0x1000U)
        return static_cast<std::size_t>(chr_banks_[addr / 0x800U]) * 0x800U + (addr & 0x7FFU);
    return static_cast<std::size_t>(chr_banks_[2U + (addr - 0x1000U) / 0x400U]) * 0x400U +
           (addr & 0x3FFU);
}

Byte TaitoTc::read_chr(Address addr) {
    const auto offset = chr_offset(addr);
    return chr_rom_.empty() ? chr_ram_[offset % chr_ram_size_] : chr_rom_[offset % chr_rom_.size()];
}

void TaitoTc::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty())
        chr_ram_[chr_offset(addr) % chr_ram_size_] = value;
}

MirrorMode TaitoTc::mirror_mode() const {
    return mirror_ != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void TaitoTc::clock_irq_counter() {
    if (!has_irq_)
        return;
    if (irq_reload_pending_ != 0U) {
        irq_counter_ = irq_reload_;
        irq_reload_pending_ = 0U;
        return;
    }
    const bool wrapped = irq_counter_ == 0xFFU;
    irq_counter_ = static_cast<Byte>(irq_counter_ + 1U);
    if (wrapped && irq_latched_ == 0U) {
        irq_latched_ = 1U;
        // The renderer currently emits an MMC3-style dot-260 pulse, not
        // individual /RD-qualified falling A12 edges. This is a compatibility
        // offset from that pulse; it is not an ASIC M2 propagation measurement.
        irq_delay_ = irq_delay_cycles_;
    }
}

void TaitoTc::clock_cpu() {
    if (irq_delay_ != 0U)
        --irq_delay_;
}

bool TaitoTc::irq_pending() const {
    return irq_enabled_ != 0U && irq_latched_ != 0U && irq_delay_ == 0U;
}

void TaitoTc::save_state(StateWriter& writer) const {
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(mirror_);
    writer.write(irq_reload_);
    writer.write(irq_counter_);
    writer.write(irq_reload_pending_);
    writer.write(irq_enabled_);
    writer.write(irq_latched_);
    writer.write(irq_delay_);
    writer.write_array(chr_ram_);
}

void TaitoTc::load_state(StateReader& reader) {
    reader.read_array(prg_banks_);
    reader.read_array(chr_banks_);
    reader.read(mirror_);
    reader.read(irq_reload_);
    reader.read(irq_counter_);
    reader.read(irq_reload_pending_);
    reader.read(irq_enabled_);
    reader.read(irq_latched_);
    reader.read(irq_delay_);
    reader.read_array(chr_ram_);
    if (!reader.ok() || mirror_ > 1U || irq_reload_pending_ > 1U || irq_enabled_ > 1U ||
        irq_latched_ > 1U || irq_delay_ > irq_delay_cycles_ ||
        (irq_delay_ != 0U && irq_latched_ == 0U) ||
        (!next_edge_reload_ && irq_reload_pending_ != 0U) ||
        std::ranges::any_of(prg_banks_, [](Byte bank) { return bank > 0x3FU; }) ||
        (!has_irq_ && (irq_reload_ != 0U || irq_counter_ != 0U || irq_enabled_ != 0U ||
                       irq_latched_ != 0U || irq_delay_ != 0U))) {
        reset();
        reader.fail();
    }
}

} // namespace mapperbus::core
