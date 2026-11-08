#include "core/mappers/konami_vrc.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
std::size_t ram_bytes(const INesHeader& header) {
    if (header.mapper_number == 75U)
        return 0U;
    if (header.mapper_number == 22U && !header.is_nes2)
        return 0U;
    return header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size : 8192U;
}
} // namespace

// Specifications: NESdev VRC1, VRC2_and_VRC4, VRC3, VRC_IRQ.
KonamiVrc::KonamiVrc(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), prg_ram_(ram_bytes(header)),
      mapper_(header.mapper_number), submapper_(header.is_nes2 ? header.submapper : 0U),
      vrc2_(mapper_ == 22U || ((mapper_ == 23U || mapper_ == 25U) && submapper_ == 3U)),
      nvram_size_(header.is_nes2 ? header.prg_nvram_size
                                 : (header.has_battery ? prg_ram_.size() : 0U)),
      initial_mirror_(header.mirror_mode), mirror_(initial_mirror_) {
    reset();
}

void KonamiVrc::reset() {
    prg_banks_ = {0U, 1U, 0U};
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    mode_ = microwire_ = irq_control_ = irq_enabled_ = irq_pending_ = 0U;
    irq_latch_ = irq_counter_ = 0U;
    irq_prescaler_ = 341U;
}

Byte KonamiVrc::port(Address address) const {
    const auto addr = static_cast<unsigned>(address);
    if (mapper_ == 21U) {
        if (submapper_ == 1U)
            return (addr >> 1U) & 3U;
        if (submapper_ == 2U)
            return (addr >> 6U) & 3U;
        return ((addr >> 1U) | (addr >> 6U)) & 3U;
    }
    if (mapper_ == 23U) {
        if (submapper_ == 2U)
            return (addr >> 2U) & 3U;
        if (submapper_ == 1U || submapper_ == 3U)
            return addr & 3U;
        return (addr | (addr >> 2U)) & 3U;
    }
    const auto normal = static_cast<Byte>(((addr >> 1U) & 1U) | ((addr & 1U) << 1U));
    if (mapper_ == 22U || submapper_ == 1U || submapper_ == 3U)
        return normal;
    const auto alternate = static_cast<Byte>(((addr >> 3U) & 1U) | ((addr >> 1U) & 2U));
    return submapper_ == 2U ? alternate : normal | alternate;
}

bool KonamiVrc::maps_prg(Address addr) const {
    if (addr >= 0x8000U)
        return true;
    if (addr < 0x6000U || mapper_ == 75U)
        return false;
    if (mapper_ == 73U)
        return !prg_ram_.empty();
    if (vrc2_)
        return !prg_ram_.empty() || addr < 0x7000U;
    return (mode_ & 1U) != 0U && !prg_ram_.empty() && (prg_ram_.size() >= 8192U || addr < 0x7000U);
}

Byte KonamiVrc::prg_read_mask(Address addr) const {
    return vrc2_ && prg_ram_.empty() && addr < 0x8000U ? 1U : 0xFFU;
}

Byte KonamiVrc::read_prg(Address addr) {
    if (addr < 0x8000U) {
        if (!maps_prg(addr))
            return 0U;
        if (prg_ram_.empty())
            return mapper_ == 23U ? microwire_ : 0U;
        return prg_ram_[(addr - 0x6000U) % prg_ram_.size()];
    }
    if (prg_rom_.empty())
        return 0U;
    if (mapper_ == 73U) {
        const std::size_t bank = addr < 0xC000U ? prg_banks_[0U] : prg_rom_.size() / 0x4000U - 1U;
        return prg_rom_[(bank * 0x4000U + (addr & 0x3FFFU)) % prg_rom_.size()];
    }
    const auto slot = (addr - 0x8000U) >> 13U;
    const std::size_t count = prg_rom_.size() / 0x2000U;
    std::size_t bank = count - 1U;
    if (mapper_ == 75U) {
        if (slot < 3U)
            bank = prg_banks_[slot];
    } else if (slot == 1U) {
        bank = prg_banks_[1U];
    } else if (slot != 3U) {
        const bool swap = !vrc2_ && (mode_ & 2U) != 0U;
        bank = (slot == 0U) != swap ? prg_banks_[0U] : count - 2U;
    }
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void KonamiVrc::irq_control(Byte data) {
    const auto value = static_cast<unsigned>(data);
    irq_control_ = value & 7U;
    irq_enabled_ = (value >> 1U) & 1U;
    irq_pending_ = 0U;
    if (irq_enabled_ != 0U) {
        irq_counter_ = irq_latch_;
        irq_prescaler_ = 341U;
    }
}

void KonamiVrc::irq_acknowledge() {
    irq_pending_ = 0U;
    irq_enabled_ = irq_control_ & 1U;
}

void KonamiVrc::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (maps_prg(addr) && !prg_ram_.empty())
            prg_ram_[(addr - 0x6000U) % prg_ram_.size()] = value;
        else if (vrc2_ && addr < 0x7000U)
            microwire_ = value & 1U;
        return;
    }
    if (addr < 0x8000U)
        return;
    const auto group = static_cast<unsigned>(addr) >> 12U;
    if (mapper_ == 75U) {
        if (group == 8U || group == 10U || group == 12U)
            prg_banks_[(group - 8U) / 2U] = value & 15U;
        else if (group == 9U) {
            if (initial_mirror_ != MirrorMode::FourScreen)
                mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
            chr_banks_[0U] = (chr_banks_[0U] & 15U) | ((value & 2U) << 3U);
            chr_banks_[1U] = (chr_banks_[1U] & 15U) | ((value & 4U) << 2U);
        } else if (group == 14U || group == 15U) {
            auto& bank = chr_banks_[group - 14U];
            bank = (bank & 16U) | (value & 15U);
        }
        return;
    }
    if (mapper_ == 73U) {
        if (group >= 8U && group <= 11U) {
            const auto shift = (group - 8U) * 4U;
            irq_latch_ =
                static_cast<uint16_t>((irq_latch_ & ~(0xFU << shift)) | ((value & 15U) << shift));
        } else if (group == 12U)
            irq_control(value);
        else if (group == 13U)
            irq_acknowledge();
        else if (group == 15U)
            prg_banks_[0U] = value & 7U;
        return;
    }
    const auto index = port(addr);
    if (group == 8U || group == 10U) {
        prg_banks_[group == 8U ? 0U : 1U] = value & 31U;
    } else if (group == 9U) {
        if (vrc2_ || index == 0U) {
            constexpr std::array kModes = {MirrorMode::Vertical,
                                           MirrorMode::Horizontal,
                                           MirrorMode::SingleLower,
                                           MirrorMode::SingleUpper};
            mirror_ = kModes[value & (vrc2_ ? 1U : 3U)];
        } else if (index == 2U)
            mode_ = value & 3U;
    } else if (group >= 11U && group <= 14U) {
        auto& bank = chr_banks_[(group - 11U) * 2U + (index >> 1U)];
        if ((index & 1U) == 0U)
            bank = (bank & 0x1F0U) | (value & 15U);
        else
            bank = (bank & 15U) | ((value & (vrc2_ ? 15U : 31U)) << 4U);
    } else if (group == 15U && !vrc2_) {
        switch (index) {
        case 0U:
            irq_latch_ = (irq_latch_ & 0xF0U) | (value & 15U);
            break;
        case 1U:
            irq_latch_ = (irq_latch_ & 15U) | ((value & 15U) << 4U);
            break;
        case 2U:
            irq_control(value);
            break;
        case 3U:
            irq_acknowledge();
            break;
        default:
            break;
        }
    }
}

std::size_t KonamiVrc::chr_offset(Address addr) const {
    if (mapper_ == 73U)
        return addr;
    if (mapper_ == 75U)
        return chr_banks_[addr >> 12U] * 0x1000U + (addr & 0x0FFFU);
    const auto bank = mapper_ == 22U ? chr_banks_[addr >> 10U] >> 1U : chr_banks_[addr >> 10U];
    return bank * 0x400U + (addr & 0x03FFU);
}
Byte KonamiVrc::read_chr(Address addr) {
    const auto offset = chr_offset(addr & 0x1FFFU);
    return chr_rom_.empty() ? chr_ram_[offset % chr_ram_.size()]
                            : chr_rom_[offset % chr_rom_.size()];
}
void KonamiVrc::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty())
        chr_ram_[chr_offset(addr & 0x1FFFU) % chr_ram_.size()] = value;
}
MirrorMode KonamiVrc::mirror_mode() const {
    return mirror_;
}

void KonamiVrc::irq_tick() {
    const uint32_t mask = mapper_ == 73U && (irq_control_ & 4U) == 0U ? 0xFFFFU : 0x00FFU;
    if ((irq_counter_ & mask) == mask) {
        irq_counter_ = static_cast<uint16_t>((irq_counter_ & ~mask) | (irq_latch_ & mask));
        irq_pending_ = 1U;
    } else
        ++irq_counter_;
}
void KonamiVrc::clock_cpu() {
    if (vrc2_ || mapper_ == 75U || irq_enabled_ == 0U)
        return;
    if (mapper_ == 73U || (irq_control_ & 4U) != 0U)
        irq_tick();
    else if (irq_prescaler_ <= 3U) {
        irq_prescaler_ += 338U;
        irq_tick();
    } else
        irq_prescaler_ -= 3U;
}
bool KonamiVrc::irq_pending() const {
    return irq_pending_ != 0U;
}
void KonamiVrc::acknowledge_irq() {
    irq_pending_ = 0U;
}

void KonamiVrc::save_state(StateWriter& w) const {
    w.write_array(prg_banks_);
    w.write_array(chr_banks_);
    w.write(mirror_);
    w.write(mode_);
    w.write(microwire_);
    w.write(irq_latch_);
    w.write(irq_counter_);
    w.write(irq_prescaler_);
    w.write(irq_control_);
    w.write(irq_enabled_);
    w.write(irq_pending_);
    w.write_bytes(prg_ram_);
    w.write_array(chr_ram_);
}
void KonamiVrc::load_state(StateReader& r) {
    r.read_array(prg_banks_);
    r.read_array(chr_banks_);
    r.read(mirror_);
    r.read(mode_);
    r.read(microwire_);
    r.read(irq_latch_);
    r.read(irq_counter_);
    r.read(irq_prescaler_);
    r.read(irq_control_);
    r.read(irq_enabled_);
    r.read(irq_pending_);
    const auto expected = prg_ram_.size();
    r.read_bytes(prg_ram_, expected);
    r.read_array(chr_ram_);
    if (prg_ram_.size() != expected || mirror_ > MirrorMode::SingleUpper || mode_ > 3U ||
        microwire_ > 1U || irq_control_ > 7U || irq_enabled_ > 1U || irq_pending_ > 1U ||
        irq_prescaler_ == 0U || irq_prescaler_ > 341U ||
        std::ranges::any_of(chr_banks_, [](uint16_t bank) { return bank > 511U; })) {
        prg_ram_.resize(expected);
        reset();
        r.fail();
    }
}
std::span<const Byte> KonamiVrc::battery_ram() const {
    return std::span<const Byte>(prg_ram_).last(std::min(nvram_size_, prg_ram_.size()));
}
void KonamiVrc::set_battery_ram(std::span<const Byte> data) {
    const auto count = std::min(nvram_size_, prg_ram_.size());
    std::copy_n(data.begin(), std::min(data.size(), count), prg_ram_.end() - count);
}

} // namespace mapperbus::core
