#include "core/mappers/irem.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev mapper 32 r23869, mapper 65 r24119; physical H3001 investigation:
// https://forums.nesdev.org/viewtopic.php?t=19778 (two PRG registers, not three).
Irem::Irem(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), h3001_(header.mapper_number == 65U),
      fixed_control_(!h3001_ && header.is_nes2 && header.submapper == 1U),
      initial_mirror_(header.mirror_mode),
      prg_ram_size_(header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size
                                   : (header.has_battery ? 0x2000U : 0U)),
      nvram_size_(header.is_nes2 ? header.prg_nvram_size : (header.has_battery ? 0x2000U : 0U)),
      chr_ram_size_(header.chr_ram_size != 0U ? header.chr_ram_size : 0x2000U),
      mirror_(initial_mirror_) {
    reset();
}

void Irem::reset() {
    // Deterministic compatibility defaults, not guaranteed physical power-up values.
    prg_banks_ = {0U, 1U};
    chr_banks_.fill(0U);
    swap_prg_ = 0U;
    mirror_ = fixed_control_ ? MirrorMode::SingleUpper : initial_mirror_;
    irq_reload_ = 0U;
    irq_counter_ = 0U;
    irq_enabled_ = 0U;
    irq_pending_ = 0U;
}

bool Irem::maps_prg(Address addr) const {
    return addr >= 0x8000U || (prg_ram_size_ != 0U && addr >= 0x6000U);
}

Byte Irem::read_prg(Address addr) {
    if (addr < 0x8000U)
        return prg_ram_size_ != 0U && addr >= 0x6000U ? prg_ram_[(addr & 0x1FFFU) % prg_ram_size_]
                                                      : 0U;
    const auto slot = (addr - 0x8000U) / 0x2000U;
    const auto count = prg_rom_.size() / 0x2000U;
    std::size_t bank = count - 1U;
    if (slot == 1U)
        bank = prg_banks_[1U];
    else if (slot == 0U || slot == 2U)
        bank = slot == (swap_prg_ != 0U ? 2U : 0U) ? prg_banks_[0U] : count - 2U;
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void Irem::write_prg(Address address, Byte value) {
    const auto addr = static_cast<unsigned>(address);
    if (addr < 0x8000U) {
        if (prg_ram_size_ != 0U && addr >= 0x6000U)
            prg_ram_[(addr & 0x1FFFU) % prg_ram_size_] = value;
        return;
    }
    switch (addr & 0xF000U) {
    case 0x8000U:
        prg_banks_[0U] = value & (h3001_ ? 0x3FU : 0x1FU);
        break;
    case 0xA000U:
        prg_banks_[1U] = value & (h3001_ ? 0x3FU : 0x1FU);
        break;
    case 0xB000U:
        chr_banks_[addr & 7U] = value;
        break;
    case 0x9000U:
        if (!h3001_) {
            if (!fixed_control_) {
                swap_prg_ = (static_cast<unsigned>(value) >> 1U) & 1U;
                mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
            }
            break;
        }
        switch (addr & 7U) {
        case 0U:
            swap_prg_ = (value & 0x80U) != 0U ? 1U : 0U;
            break;
        case 1U:
            mirror_ = (value & 0x40U) != 0U   ? MirrorMode::SingleLower
                      : (value & 0x80U) != 0U ? MirrorMode::Horizontal
                                              : MirrorMode::Vertical;
            break;
        case 3U:
            irq_enabled_ = (value & 0x80U) != 0U ? 1U : 0U;
            irq_pending_ = 0U;
            break;
        case 4U:
            irq_counter_ = irq_reload_;
            irq_pending_ = 0U;
            break;
        case 5U:
            irq_reload_ = (irq_reload_ & 0x00FFU) | (static_cast<unsigned>(value) << 8U);
            break;
        case 6U:
            irq_reload_ = (irq_reload_ & 0xFF00U) | value;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

std::size_t Irem::chr_offset(Address addr) const {
    const auto offset = static_cast<unsigned>(addr) & 0x1FFFU;
    return static_cast<std::size_t>(chr_banks_[offset / 0x400U]) * 0x400U + (offset & 0x3FFU);
}

Byte Irem::read_chr(Address addr) {
    const auto offset = chr_offset(addr);
    return chr_rom_.empty() ? chr_ram_[offset % chr_ram_size_] : chr_rom_[offset % chr_rom_.size()];
}

void Irem::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty())
        chr_ram_[chr_offset(addr) % chr_ram_size_] = value;
}

MirrorMode Irem::mirror_mode() const {
    return mirror_;
}

void Irem::clock_cpu() {
    if (h3001_ && irq_enabled_ != 0U && irq_counter_ != 0U) {
        --irq_counter_;
        if (irq_counter_ == 0U)
            irq_pending_ = 1U;
    }
}

bool Irem::irq_pending() const {
    return irq_pending_ != 0U;
}

void Irem::save_state(StateWriter& writer) const {
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(swap_prg_);
    writer.write(mirror_);
    writer.write(irq_reload_);
    writer.write(irq_counter_);
    writer.write(irq_enabled_);
    writer.write(irq_pending_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Irem::load_state(StateReader& reader) {
    reader.read_array(prg_banks_);
    reader.read_array(chr_banks_);
    reader.read(swap_prg_);
    reader.read(mirror_);
    reader.read(irq_reload_);
    reader.read(irq_counter_);
    reader.read(irq_enabled_);
    reader.read(irq_pending_);
    reader.read_array(prg_ram_);
    reader.read_array(chr_ram_);
    if (!reader.ok() || swap_prg_ > 1U || irq_enabled_ > 1U || irq_pending_ > 1U ||
        mirror_ > MirrorMode::SingleUpper ||
        (irq_pending_ != 0U && (irq_enabled_ == 0U || irq_counter_ != 0U)) ||
        std::ranges::any_of(prg_banks_,
                            [this](Byte bank) { return bank > (h3001_ ? 63U : 31U); }) ||
        (fixed_control_ && (swap_prg_ != 0U || mirror_ != MirrorMode::SingleUpper)) ||
        (!h3001_ &&
         (irq_reload_ != 0U || irq_counter_ != 0U || irq_enabled_ != 0U || irq_pending_ != 0U))) {
        reset();
        reader.fail();
    }
}

std::span<const Byte> Irem::battery_ram() const {
    return std::span<const Byte>(prg_ram_).subspan(prg_ram_size_ - nvram_size_, nvram_size_);
}

void Irem::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(),
                std::min(data.size(), nvram_size_),
                prg_ram_.begin() + static_cast<std::ptrdiff_t>(prg_ram_size_ - nvram_size_));
}

} // namespace mapperbus::core
