#include "core/mappers/rambo1.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

// Banking and IRQ rules: teaguecl/nes_tengen_rambo1_research, revision
// 544ca1b22efe18ce233f521b5a4a9c1def12c697, docs/emulator_guide.md (2026-11-02).
// CIRAM variant: NESdev mapper 158 r21299. Implementation is original.
Rambo1::Rambo1(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), chr_ciram_(header.mapper_number == 158U) {
}

void Rambo1::reset() {
    // The console reset line does not reach this chip. Member initializers
    // define a deterministic cold start; short power-off analog retention is
    // outside the emulator's reset model.
}

Byte Rambo1::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    auto slot = (static_cast<unsigned>(addr) - 0x8000U) / 0x2000U;
    if ((select_ & 0x40U) != 0U && (slot == 0U || slot == 2U))
        slot ^= 2U;
    const std::size_t bank = slot == 3U ? 31U : prg_banks_[slot];
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void Rambo1::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    switch (addr & 0xE001U) {
    case 0x8000U:
        select_ = value & 0xEFU;
        break;
    case 0x8001U: {
        const auto reg = select_ & 15U;
        if (reg < 6U)
            chr_banks_[reg] = value;
        else if (reg == 6U || reg == 7U)
            prg_banks_[reg - 6U] = value & 31U;
        else if (reg == 8U || reg == 9U)
            chr_banks_[reg - 2U] = value;
        else if (reg == 15U)
            prg_banks_[2] = value & 31U;
        break;
    }
    case 0xA000U:
        if (!chr_ciram_)
            mirror_ = value & 1U;
        break;
    case 0xC000U:
        latch_ = value;
        break;
    case 0xC001U: {
        const bool early = cpu_mode_ == 0U && filter_ < 16U;
        const auto mode = value & 1U;
        if (mode != cpu_mode_)
            irq_delay_ = 0U;
        cpu_mode_ = mode;
        reload_ = cpu_mode_ == 0U ? 1U : 0U;
        counter_ = 0U;
        if (filter_ == 16U)
            filter_ = 17U; // Swallow the next rise after a full low interval.
        load_delay_ = cpu_mode_ != 0U ? (early ? 1U : 5U) : 0U;
        cpu_remaining_ = 0U;
        restart_cycle_ = 1U;
        break;
    }
    case 0xE000U:
        enabled_ = 0U;
        pending_ = 0U;
        break;
    case 0xE001U:
        enabled_ = 1U;
        break;
    default:
        break;
    }
}

Byte Rambo1::chr_bank(Address addr) const {
    const auto slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((select_ & 0x80U) != 0U ? 4U : 0U);
    if (slot >= 4U)
        return chr_banks_[slot - 2U];
    if ((select_ & 0x20U) == 0U)
        return static_cast<Byte>((chr_banks_[slot / 2U] & 0xFEU) | (slot & 1U));
    constexpr std::array<unsigned, 4> kRegisters = {0U, 6U, 1U, 7U};
    return chr_banks_[kRegisters[slot]];
}

Byte Rambo1::read_chr(Address addr) {
    return chr_rom_[(static_cast<std::size_t>(chr_bank(addr)) * 0x400U + (addr & 0x3FFU)) %
                    chr_rom_.size()];
}

void Rambo1::write_chr(Address, Byte) {}

MirrorMode Rambo1::mirror_mode() const {
    // Arbitrary mapper-158 arrangements are represented by nametable_address.
    return mirror_ != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Address> Rambo1::nametable_address(Address addr) const {
    if (!chr_ciram_)
        return std::nullopt;
    return static_cast<Address>(((chr_bank(addr & 0xFFFU) & 0x80U) << 3U) | (addr & 0x3FFU));
}

bool Rambo1::observes_ppu_a12() const {
    return true;
}

void Rambo1::observe_ppu_a12(bool high) {
    a12_ = high ? 1U : 0U;
}

bool Rambo1::irq_pending() const {
    return pending_ != 0U;
}

void Rambo1::clock_scanline_counter() {
    bool expired = false;
    if (reload_ != 0U) {
        counter_ = latch_;
        reload_ = 0U;
        expired = counter_ == 0U;
    } else if (counter_ == 0U) {
        counter_ = latch_;
    } else {
        --counter_;
        expired = counter_ == 0U;
    }
    if (expired)
        irq_delay_ = 1U; // Enable is tested at the next fall, not this clock.
}

void Rambo1::clock_cpu_falling_edge() {
    const auto restart = std::exchange(restart_cycle_, 0U);
    if (irq_delay_ != 0U) {
        irq_delay_ = 0U;
        if (enabled_ != 0U)
            pending_ = 1U;
    }
    if (a12_ == 0U) {
        if (filter_ < 16U)
            ++filter_;
    } else {
        if (cpu_mode_ == 0U && filter_ == 16U)
            clock_scanline_counter();
        filter_ = 0U;
    }
    if (cpu_mode_ == 0U || restart != 0U)
        return;
    if (load_delay_ != 0U) {
        if (--load_delay_ == 0U)
            cpu_remaining_ = static_cast<uint16_t>(4U * latch_ + 1U);
    } else if (cpu_remaining_ != 0U) {
        --cpu_remaining_;
        if (cpu_remaining_ == 0U) {
            if (enabled_ != 0U)
                pending_ = 1U;
            cpu_remaining_ = static_cast<uint16_t>(4U * (static_cast<unsigned>(latch_) + 1U));
        }
    }
}

void Rambo1::save_state(StateWriter& writer) const {
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(select_);
    writer.write(mirror_);
    writer.write(latch_);
    writer.write(counter_);
    writer.write(cpu_mode_);
    writer.write(reload_);
    writer.write(enabled_);
    writer.write(pending_);
    writer.write(filter_);
    writer.write(a12_);
    writer.write(irq_delay_);
    writer.write(restart_cycle_);
    writer.write(load_delay_);
    writer.write(cpu_remaining_);
}

void Rambo1::load_state(StateReader& reader) {
    reader.read_array(prg_banks_);
    reader.read_array(chr_banks_);
    reader.read(select_);
    reader.read(mirror_);
    reader.read(latch_);
    reader.read(counter_);
    reader.read(cpu_mode_);
    reader.read(reload_);
    reader.read(enabled_);
    reader.read(pending_);
    reader.read(filter_);
    reader.read(a12_);
    reader.read(irq_delay_);
    reader.read(restart_cycle_);
    reader.read(load_delay_);
    reader.read(cpu_remaining_);
    if (!reader.ok() || (select_ & 0x10U) != 0U || mirror_ > 1U || (chr_ciram_ && mirror_ != 0U) ||
        cpu_mode_ > 1U || reload_ > 1U || enabled_ > 1U || pending_ > 1U || filter_ > 17U ||
        a12_ > 1U || irq_delay_ > 1U || restart_cycle_ > 1U || load_delay_ > 5U ||
        cpu_remaining_ > 1024U ||
        (cpu_mode_ == 0U && (load_delay_ != 0U || cpu_remaining_ != 0U)) ||
        (cpu_mode_ != 0U && (reload_ != 0U || irq_delay_ != 0U)) ||
        (load_delay_ != 0U && cpu_remaining_ != 0U) ||
        std::ranges::any_of(prg_banks_, [](Byte bank) { return bank > 31U; }))
        reader.fail();
}

} // namespace mapperbus::core
