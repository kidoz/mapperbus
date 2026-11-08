#include "core/mappers/sunsoft34.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev mapper 67 r23676, mapper 68 r23720, and NES 2.0 submapper 68:1.
Sunsoft34::Sunsoft34(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), sunsoft4_(header.mapper_number == 68U),
      dcs_(sunsoft4_ && header.is_nes2 && header.submapper == 1U),
      initial_mirror_(header.mirror_mode == MirrorMode::Horizontal ? 1U : 0U),
      prg_ram_size_(header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size
                                   : (sunsoft4_ || header.has_battery ? 0x2000U : 0U)),
      nvram_size_(header.is_nes2 ? header.prg_nvram_size : (header.has_battery ? 0x2000U : 0U)),
      chr_ram_size_(header.chr_ram_size != 0U ? header.chr_ram_size : 0x2000U) {
    reset();
}

void Sunsoft34::reset() {
    // Stable emulator defaults; RAM survives reset. DCS starts on internal ROM.
    prg_control_ = dcs_ ? 8U : 0U;
    chr_banks_.fill(0U);
    nametable_banks_.fill(0U);
    nametable_control_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_low_next_ = 0U;
    irq_enabled_ = 0U;
    irq_pending_ = 0U;
}

bool Sunsoft34::ram_enabled() const {
    return prg_ram_size_ != 0U && (!sunsoft4_ || (prg_control_ & 0x10U) != 0U);
}

bool Sunsoft34::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && ram_enabled());
}

Byte Sunsoft34::prg_read_mask(Address addr) const {
    if (addr < 0x8000U)
        return addr >= 0x6000U && ram_enabled() ? 0xFFU : 0U;
    return dcs_ && addr < 0xC000U && (prg_control_ & 8U) == 0U ? 0U : 0xFFU;
}

Byte Sunsoft34::read_prg(Address addr) {
    if (addr < 0x8000U)
        return addr >= 0x6000U && ram_enabled() ? prg_ram_[(addr & 0x1FFFU) % prg_ram_size_] : 0U;
    if (prg_read_mask(addr) == 0U)
        return 0U; // Empty DCS option slot drives no CPU data lines.
    const std::size_t bank =
        addr < 0xC000U ? prg_control_ & (dcs_ ? 7U : 15U) : prg_rom_.size() / 0x4000U - 1U;
    return prg_rom_[(bank * 0x4000U + (addr & 0x3FFFU)) % prg_rom_.size()];
}

void Sunsoft34::write_prg(Address address, Byte value) {
    const auto addr = static_cast<unsigned>(address);
    if (addr < 0x8000U) {
        if (addr >= 0x6000U && ram_enabled())
            prg_ram_[(addr & 0x1FFFU) % prg_ram_size_] = value;
        // A licensing strobe has no observable effect without an option ROM.
        return;
    }
    if (sunsoft4_) {
        const auto reg = (addr >> 12U) & 7U;
        if (reg < 4U)
            chr_banks_[reg] = value & 0x7FU;
        else if (reg < 6U)
            nametable_banks_[reg - 4U] = value & 0x7FU;
        else if (reg == 6U)
            nametable_control_ = value & 0x13U;
        else
            prg_control_ = value & 0x1FU;
        return;
    }
    if ((addr & 0x8800U) == 0x8000U) {
        irq_pending_ = 0U;
        return;
    }
    switch (addr & 0xF800U) {
    case 0x8800U:
    case 0x9800U:
    case 0xA800U:
    case 0xB800U:
        chr_banks_[(addr - 0x8800U) / 0x1000U] = value & 0x3FU;
        break;
    case 0xC800U:
        if (irq_low_next_ == 0U)
            irq_counter_ = (irq_counter_ & 0xFFU) | (static_cast<unsigned>(value) << 8U);
        else
            irq_counter_ = (irq_counter_ & 0xFF00U) | value;
        irq_low_next_ ^= 1U;
        break;
    case 0xD800U:
        irq_enabled_ = (value & 0x10U) != 0U ? 1U : 0U;
        irq_low_next_ = 0U;
        break;
    case 0xE800U:
        nametable_control_ = value & 3U;
        break;
    case 0xF800U:
        prg_control_ = value & 0x1FU;
        break;
    default:
        break;
    }
}

std::size_t Sunsoft34::chr_offset(Address addr) const {
    const auto offset = static_cast<unsigned>(addr) & 0x1FFFU;
    return static_cast<std::size_t>(chr_banks_[offset / 0x800U]) * 0x800U + (offset & 0x7FFU);
}

Byte Sunsoft34::read_chr(Address addr) {
    const auto offset = chr_offset(addr);
    return chr_rom_.empty() ? chr_ram_[offset % chr_ram_size_] : chr_rom_[offset % chr_rom_.size()];
}

void Sunsoft34::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty())
        chr_ram_[chr_offset(addr) % chr_ram_size_] = value;
}

MirrorMode Sunsoft34::mirror_mode() const {
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    return kModes[nametable_control_ & 3U];
}

std::optional<Byte> Sunsoft34::read_nametable(Address address) const {
    if (!sunsoft4_ || (nametable_control_ & 0x10U) == 0U)
        return std::nullopt;
    const auto addr = static_cast<unsigned>(address);
    const auto table = (addr >> 10U) & 3U;
    unsigned bank = 0U;
    switch (nametable_control_ & 3U) {
    case 0U:
        bank = table & 1U;
        break;
    case 1U:
        bank = table >> 1U;
        break;
    case 3U:
        bank = 1U;
        break;
    default:
        break;
    }
    const std::size_t offset = (nametable_banks_[bank] | 0x80U) * 0x400U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

bool Sunsoft34::write_nametable(Address /*addr*/, Byte /*value*/) {
    return sunsoft4_ && (nametable_control_ & 0x10U) != 0U;
}

void Sunsoft34::clock_cpu() {
    if (!sunsoft4_ && irq_enabled_ != 0U) {
        const bool underflow = irq_counter_ == 0U;
        --irq_counter_;
        if (underflow) {
            irq_pending_ = 1U;
            irq_enabled_ = 0U;
        }
    }
}

bool Sunsoft34::irq_pending() const {
    return irq_pending_ != 0U;
}

std::span<const Byte> Sunsoft34::battery_ram() const {
    return std::span<const Byte>(prg_ram_).subspan(prg_ram_size_ - nvram_size_, nvram_size_);
}

void Sunsoft34::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(),
                std::min(data.size(), nvram_size_),
                prg_ram_.begin() + static_cast<std::ptrdiff_t>(prg_ram_size_ - nvram_size_));
}

void Sunsoft34::save_state(StateWriter& writer) const {
    writer.write(prg_control_);
    writer.write_array(chr_banks_);
    writer.write_array(nametable_banks_);
    writer.write(nametable_control_);
    writer.write(irq_counter_);
    writer.write(irq_low_next_);
    writer.write(irq_enabled_);
    writer.write(irq_pending_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Sunsoft34::load_state(StateReader& reader) {
    reader.read(prg_control_);
    reader.read_array(chr_banks_);
    reader.read_array(nametable_banks_);
    reader.read(nametable_control_);
    reader.read(irq_counter_);
    reader.read(irq_low_next_);
    reader.read(irq_enabled_);
    reader.read(irq_pending_);
    reader.read_array(prg_ram_);
    reader.read_array(chr_ram_);
    if (!reader.ok() || prg_control_ > 0x1FU || irq_low_next_ > 1U || irq_enabled_ > 1U ||
        irq_pending_ > 1U || (nametable_control_ & ~(sunsoft4_ ? 0x13U : 3U)) != 0U ||
        std::ranges::any_of(chr_banks_,
                            [this](Byte bank) { return bank > (sunsoft4_ ? 127U : 63U); }) ||
        std::ranges::any_of(nametable_banks_, [](Byte bank) { return bank > 127U; }) ||
        (sunsoft4_ &&
         (irq_counter_ != 0U || irq_low_next_ != 0U || irq_enabled_ != 0U || irq_pending_ != 0U)) ||
        (!sunsoft4_ && (nametable_banks_[0U] != 0U || nametable_banks_[1U] != 0U))) {
        reset();
        reader.fail();
    }
}

} // namespace mapperbus::core
