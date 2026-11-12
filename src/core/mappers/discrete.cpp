#include "core/mappers/discrete.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kMapper202StateTag = 0xCA00U;
} // namespace

// docs/reference/mapper-implementation.md records NESdev source revisions for
// these explicit board cases, including unresolved electrical behavior.
DiscreteMapper::DiscreteMapper(const INesHeader& header,
                               std::vector<Byte> prg,
                               std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), mapper_(header.mapper_number),
      submapper_(header.is_nes2 ? header.submapper : 0U), initial_mirror_(header.mirror_mode),
      nina_(mapper_ == 34U &&
            (submapper_ == 1U || (submapper_ == 0U && chr_rom_.size() > 0x2000U))),
      holy_diver_(submapper_ == 3U ||
                  (submapper_ == 0U && header.mirror_mode == MirrorMode::FourScreen)),
      mirror_(initial_mirror_) {
    reset();
    if (mapper_ == 28U) {
        registers_[3U] = 0xFFU;
        mirror_ = MirrorMode::SingleLower;
    } else if (mapper_ == 31U) {
        registers_[7U] = 0xFFU;
    }
}

void DiscreteMapper::reset() {
    // These homebrew boards have power-up defaults but no reset input.
    if (mapper_ == 28U || mapper_ == 31U)
        return;
    registers_.fill(0U);
    address_latch_ = 0U;
    mirror_ = initial_mirror_;
    if (mapper_ == 13U)
        mirror_ = MirrorMode::Vertical;
    if (mapper_ == 71U && submapper_ == 1U)
        mirror_ = MirrorMode::SingleLower;
    if (mapper_ == 78U)
        mirror_ = holy_diver_ ? MirrorMode::Horizontal : MirrorMode::SingleLower;
    if (mapper_ == 89U || mapper_ == 152U)
        mirror_ = MirrorMode::SingleLower;
    if (mapper_ == 97U)
        mirror_ = MirrorMode::Horizontal;
    if (mapper_ == 113U)
        mirror_ = MirrorMode::Horizontal;
    switch (mapper_) {
    case 38U:
    case 58U:
    case 61U:
    case 62U:
    case 200U:
    case 202U:
    case 212U:
    case 213U:
    case 225U:
    case 229U:
    case 231U:
        mirror_ = MirrorMode::Vertical;
        break;
    case 226U:
        mirror_ = MirrorMode::Horizontal;
        break;
    default:
        break;
    }
}

bool DiscreteMapper::has_prg_ram() const {
    return nina_ || mapper_ == 240U;
}

bool DiscreteMapper::maps_prg(Address addr) const {
    return addr >= 0x8000U || ((has_prg_ram() || mapper_ == 212U) && addr >= 0x6000U);
}

std::size_t DiscreteMapper::prg_offset(Address address) const {
    const std::size_t addr = address;
    const std::size_t latched_address = address_latch_;
    const std::size_t secondary = registers_[1U];

    const std::size_t value = registers_[0U];
    std::size_t bank = 0U;
    bool bank_16k = false;
    switch (mapper_) {
    case 28U: {
        const auto mode = (static_cast<unsigned>(registers_[2U]) >> 2U) & 3U;
        const auto size = (static_cast<unsigned>(registers_[2U]) >> 4U) & 3U;
        const std::size_t mask = (std::size_t{2U} << size) - 1U;
        const std::size_t outer = static_cast<std::size_t>(registers_[3U]) * 2U;
        const auto high = (addr >> 14U) & 1U;
        if ((mode == 2U && high == 0U) || (mode == 3U && high != 0U))
            bank = outer | high;
        else {
            const std::size_t inner = mode < 2U ? secondary * 2U + high : secondary;
            bank = (outer & ~mask) | (inner & mask);
        }
        return bank * 0x4000U + (addr & 0x3FFFU);
    }
    case 31U:
        return registers_[(addr - 0x8000U) >> 12U] * std::size_t{0x1000} + (addr & 0x0FFFU);
    case 38U:
        bank = value & 3U;
        break;
    case 46U:
        bank = ((secondary & 15U) << 1U) | (value & 1U);
        break;
    case 58U:
    case 213U:
        bank = latched_address & 7U;
        if ((latched_address & 0x40U) == 0U)
            bank = (bank & ~std::size_t{1U}) | ((addr >> 14U) & 1U);
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 61U:
        bank = (latched_address & 15U) * 2U;
        bank |= (latched_address & 0x10U) != 0U ? (latched_address >> 5U) & 1U : (addr >> 14U) & 1U;
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 62U:
        bank = ((latched_address >> 8U) & 0x3FU) | (latched_address & 0x40U);
        if ((latched_address & 0x20U) == 0U)
            bank = (bank & ~std::size_t{1U}) | ((addr >> 14U) & 1U);
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 81U:
        bank = (latched_address >> 2U) & 3U;
        bank_16k = true;
        break;
    case 107U:
        bank = value >> 1U;
        break;
    case 148U:
        bank = (value >> 3U) & 1U;
        break;
    case 193U:
        bank = addr < 0xA000U ? registers_[3U] & 15U
                              : prg_rom_.size() / 0x2000U - 4U + ((addr - 0x8000U) >> 13U);
        return bank * 0x2000U + (addr & 0x1FFFU);
    case 200U:
        bank = latched_address & (submapper_ == 1U ? 7U : 15U);
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 201U:
        bank = latched_address & 0xFFU;
        break;
    case 202U:
        bank = (latched_address >> 1U) & 7U;
        // Compatibility interpretation of NESdev r5551: the upper window
        // advances one 16 KiB page in 32 KiB mode, without pair alignment.
        if ((latched_address & 9U) == 9U && addr >= 0xC000U)
            bank = (bank + 1U) & 7U;
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 203U:
        return (value >> 2U) * 0x4000U + (addr & 0x3FFFU);
    case 212U:
        bank = latched_address & 7U;
        if ((latched_address & 0x4000U) != 0U)
            bank = (bank & ~std::size_t{1U}) | ((addr >> 14U) & 1U);
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 225U:
        bank = ((latched_address >> 6U) & 0x3FU) | ((latched_address >> 8U) & 0x40U);
        if ((latched_address & 0x1000U) == 0U)
            bank = (bank & ~std::size_t{1U}) | ((addr >> 14U) & 1U);
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 226U:
        bank = (value & 31U) | ((value >> 2U) & 32U) | ((secondary & 1U) << 6U);
        if ((value & 0x20U) == 0U)
            bank = (bank & ~std::size_t{1U}) | ((addr >> 14U) & 1U);
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 229U:
        bank = latched_address & 31U;
        if (bank == 0U)
            bank = (addr >> 14U) & 1U;
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 231U:
        bank = (latched_address & 0x1EU) | ((latched_address >> 5U) & 1U);
        if (addr < 0xC000U)
            bank &= ~std::size_t{1U};
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 232U:
        bank = (secondary << 2U) | (addr < 0xC000U ? value & 3U : 3U);
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 240U:
        bank = value >> 4U;
        break;
    case 34U:
        bank = value;
        break;
    case 66U:
    case 140U:
        bank = value >> 4U;
        break;
    case 79U:
        bank = (value >> 3U) & 1U;
        break;
    case 113U:
        bank = (value >> 3U) & 7U;
        break;
    case 70U:
        bank = value >> 4U;
        bank_16k = true;
        break;
    case 71U:
        bank = value & (submapper_ == 1U ? 7U : 15U);
        bank_16k = true;
        break;
    case 78U:
        bank = value & 7U;
        bank_16k = true;
        break;
    case 89U:
    case 93U:
        bank = (value >> 4U) & 7U;
        bank_16k = true;
        break;
    case 94U:
        bank = (value >> 2U) & 7U;
        bank_16k = true;
        break;
    case 152U:
        bank = (value >> 4U) & 7U;
        bank_16k = true;
        break;
    case 180U:
        bank = addr < 0xC000U ? 0U : value & 7U;
        return bank * 0x4000U + (addr & 0x3FFFU);
    case 97U:
        bank = addr < 0xC000U ? prg_rom_.size() / 0x4000U - 1U : value & 0x1FU;
        return bank * 0x4000U + (addr & 0x3FFFU);
    default:
        break;
    }
    if (bank_16k) {
        if (addr >= 0xC000U)
            bank = prg_rom_.size() / 0x4000U - 1U;
        return bank * 0x4000U + (addr & 0x3FFFU);
    }
    return bank * 0x8000U + (addr & 0x7FFFU);
}

Byte DiscreteMapper::read_prg(Address addr) {
    if (mapper_ == 212U && addr >= 0x6000U && addr < 0x8000U)
        return (addr & 0x10U) == 0U ? 0x80U : 0U;
    if (addr < 0x8000U)
        return has_prg_ram() && addr >= 0x6000U ? prg_ram_[addr & 0x1FFFU] : 0U;
    return prg_rom_.empty() ? 0U : prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void DiscreteMapper::write_prg(Address addr, Byte data) {
    auto value = static_cast<unsigned>(data);
    if (addr < 0x6000U)
        return;
    if (mapper_ == 31U)
        return;
    if (mapper_ == 28U) {
        if (addr < 0x8000U)
            return;
        switch (register_select_) {
        case 0U:
        case 1U:
            registers_[register_select_] = value & (register_select_ == 0U ? 3U : 15U);
            if ((registers_[2U] & 2U) == 0U)
                registers_[2U] = static_cast<Byte>((registers_[2U] & ~1U) | ((value >> 4U) & 1U));
            break;
        case 0x80U:
            registers_[2U] = value & 0x3FU;
            break;
        case 0x81U:
            registers_[3U] = value;
            break;
        default:
            break;
        }
        constexpr std::array kModes = {MirrorMode::SingleLower,
                                       MirrorMode::SingleUpper,
                                       MirrorMode::Vertical,
                                       MirrorMode::Horizontal};
        mirror_ = kModes[registers_[2U] & 3U];
        return;
    }
    if (mapper_ == 240U) {
        if (addr < 0x8000U)
            prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    if (nina_) {
        if (addr < 0x8000U) {
            prg_ram_[addr & 0x1FFFU] = value;
            if (addr >= 0x7FFDU)
                registers_[addr - 0x7FFDU] = value;
        }
        return;
    }
    if (mapper_ == 79U || mapper_ == 113U || mapper_ == 145U)
        return; // Only the expansion address decoder clocks these latches.
    if (mapper_ == 87U || mapper_ == 140U || mapper_ == 184U) {
        if (addr < 0x8000U)
            registers_[0U] = value;
        return;
    }
    if (mapper_ == 38U) {
        if (addr >= 0x7000U && addr < 0x8000U)
            registers_[0U] = value;
        return;
    }
    if (mapper_ == 46U && addr < 0x8000U) {
        registers_[1U] = value;
        return;
    }
    if (mapper_ == 193U) {
        if (addr < 0x8000U && (addr & 7U) <= 4U) {
            registers_[addr & 7U] = value;
            if ((addr & 7U) == 4U)
                mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        }
        return;
    }
    if (addr < 0x8000U)
        return;
    switch (mapper_) {
    case 202U:
        address_latch_ = addr & 15U;
        mirror_ = (addr & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        return;
    case 46U:
    case 107U:
    case 203U:
        registers_[0U] = value;
        return;
    case 58U:
    case 61U:
    case 62U:
    case 81U:
    case 200U:
    case 201U:
    case 212U:
    case 213U:
    case 225U:
    case 229U:
    case 231U: {
        address_latch_ = addr;
        registers_[0U] = value;
        Address mirror_bit = 0U;
        if (mapper_ == 58U || mapper_ == 61U || mapper_ == 62U || mapper_ == 213U ||
            mapper_ == 231U)
            mirror_bit = 0x80U;
        else if (mapper_ == 200U)
            mirror_bit = submapper_ == 1U ? 4U : 8U;
        else if (mapper_ == 212U)
            mirror_bit = 8U;
        else if (mapper_ == 225U)
            mirror_bit = 0x2000U;
        else if (mapper_ == 229U)
            mirror_bit = 0x20U;
        if (mirror_bit != 0U)
            mirror_ = (addr & mirror_bit) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        return;
    }
    case 226U:
        registers_[addr & 1U] = value;
        mirror_ = (registers_[0U] & 0x40U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        return;
    case 232U:
        if (addr < 0xC000U) {
            auto outer = (value >> 3U) & 3U;
            if (submapper_ == 1U)
                outer = static_cast<Byte>(((outer & 1U) << 1U) | (outer >> 1U));
            registers_[1U] = outer;
        } else {
            registers_[0U] = value & 3U;
        }
        return;
    default:
        break;
    }
    if (mapper_ == 71U) {
        if (addr >= 0xC000U)
            registers_[0U] = value;
        else if ((submapper_ == 1U && addr < 0xA000U) ||
                 (submapper_ == 0U && addr >= 0x9000U && addr < 0xA000U))
            mirror_ = (value & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
        return;
    }
    if (mapper_ == 97U) {
        if (addr < 0xC000U) {
            registers_[0U] = value;
            mirror_ = (value & 0x80U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        }
        return;
    }
    // All remaining boards have a latch overlapping enabled PRG-ROM outputs.
    value &= read_prg(addr);
    registers_[0U] = value;
    if (mapper_ == 78U) {
        mirror_ = holy_diver_
                      ? ((value & 8U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal)
                      : ((value & 8U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower);
    } else if (mapper_ == 89U) {
        mirror_ = (value & 8U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    } else if (mapper_ == 152U) {
        mirror_ = (value & 0x80U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
}

bool DiscreteMapper::maps_expansion(Address addr) const {
    if (mapper_ == 28U || mapper_ == 31U)
        return addr >= 0x5000U && addr < 0x6000U;
    if (mapper_ == 225U)
        return addr >= 0x5800U && addr < 0x6000U;
    if (mapper_ == 240U)
        return (addr & 0xE800U) == 0x4800U || (addr & 0xE100U) == 0x4100U;
    return (mapper_ == 79U || mapper_ == 113U || mapper_ == 145U) && (addr & 0xE100U) == 0x4100U;
}

Byte DiscreteMapper::read_expansion(Address addr) {
    return mapper_ == 225U && maps_expansion(addr) ? nibble_ram_[addr & 3U] : 0U;
}

Byte DiscreteMapper::prg_read_mask(Address addr) const {
    return mapper_ == 212U && addr < 0x8000U ? 0x8FU : 0xFFU;
}

Byte DiscreteMapper::expansion_read_mask(Address) const {
    return mapper_ == 225U ? 0x0FU : 0U;
}

void DiscreteMapper::write_expansion(Address addr, Byte value) {
    if (maps_expansion(addr)) {
        if (mapper_ == 28U) {
            register_select_ = value & 0x81U;
            return;
        }
        if (mapper_ == 31U) {
            registers_[addr & 7U] = value;
            return;
        }
        if (mapper_ == 225U) {
            nibble_ram_[addr & 3U] = value & 15U;
            return;
        }
        registers_[0U] = value;
        if (mapper_ == 113U)
            mirror_ = (value & 0x80U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    }
}

std::size_t DiscreteMapper::chr_offset(Address address) const {
    const std::size_t addr = address;
    const std::size_t latched_address = address_latch_;
    const std::size_t secondary = registers_[1U];

    const std::size_t value = registers_[0U];
    std::size_t bank = 0U;
    switch (mapper_) {
    case 28U:
        bank = registers_[0U];
        break;
    case 38U:
        bank = (value >> 2U) & 3U;
        break;
    case 46U:
        bank = ((secondary >> 4U) << 3U) | ((value >> 4U) & 7U);
        break;
    case 58U:
    case 213U:
        bank = (latched_address >> 3U) & 7U;
        break;
    case 61U:
        bank = (latched_address >> 8U) & 15U;
        if (submapper_ == 1U)
            bank = (bank << 1U) | ((latched_address >> 6U) & 1U);
        break;
    case 62U:
        bank = ((latched_address & 31U) << 2U) | (value & 3U);
        break;
    case 81U:
        bank = latched_address & 3U;
        break;
    case 107U:
        bank = value;
        break;
    case 145U:
    case 149U:
        bank = value >> 7U;
        break;
    case 148U:
        bank = value & 7U;
        break;
    case 193U:
        if (addr < 0x1000U)
            return (registers_[0U] & 0xFCU) * std::size_t{0x400} + (addr & 0x0FFFU);
        return (registers_[addr < 0x1800U ? 1U : 2U] & 0xFEU) * std::size_t{0x400} +
               (addr & 0x07FFU);
    case 200U:
        bank = latched_address & (submapper_ == 1U ? 7U : 15U);
        break;
    case 201U:
        bank = latched_address & 0xFFU;
        break;
    case 202U:
        bank = (latched_address >> 1U) & 7U;
        break;
    case 203U:
        bank = value & 3U;
        break;
    case 212U:
        bank = latched_address & 7U;
        break;
    case 225U:
        bank = (latched_address & 0x3FU) | ((latched_address >> 8U) & 0x40U);
        break;
    case 229U:
        bank = latched_address & 31U;
        break;
    case 240U:
        bank = value & 15U;
        break;
    case 13U:
        return (addr < 0x1000U ? 0U : (value & 3U) * std::size_t{0x1000}) + (addr & 0x0FFFU);
    case 34U:
        if (nina_)
            return registers_[addr < 0x1000U ? 1U : 2U] * std::size_t{0x1000} + (addr & 0x0FFFU);
        break;
    case 66U:
    case 70U:
    case 140U:
    case 152U:
        bank = value & 0x0FU;
        break;
    case 78U:
        bank = value >> 4U;
        break;
    case 79U:
        bank = value & 7U;
        break;
    case 87U:
        bank = ((value & 1U) << 1U) | ((value >> 1U) & 1U);
        break;
    case 89U:
        bank = (value & 7U) | ((value >> 4U) & 8U);
        break;
    case 93U:
        if (!chr_rom_.empty())
            bank = (value & 7U) | ((value >> 4U) & 8U);
        break;
    case 113U:
        bank = (value & 7U) | ((value >> 3U) & 8U);
        break;
    case 184U:
        bank = addr < 0x1000U ? value & 7U : ((value >> 4U) & 3U) | 4U;
        return bank * std::size_t{0x1000} + (addr & 0x0FFFU);
    default:
        break;
    }
    return bank * 0x2000U + (addr & 0x1FFFU);
}

Byte DiscreteMapper::read_chr(Address addr) {
    if (mapper_ == 93U && chr_rom_.empty() && (registers_[0U] & 1U) == 0U)
        return 0xFFU; // Undriven CHR data; exact PPU bus decay is not modeled.
    const auto offset = chr_offset(addr);
    if (!chr_rom_.empty())
        return chr_rom_[offset % chr_rom_.size()];
    const auto capacity = mapper_ == 28U ? 0x8000U : (mapper_ == 13U ? 0x4000U : 0x2000U);
    return chr_ram_[offset % capacity];
}

void DiscreteMapper::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty() && !(mapper_ == 93U && (registers_[0U] & 1U) == 0U) &&
        !(mapper_ == 226U && (registers_[1U] & 2U) != 0U))
        chr_ram_[chr_offset(addr) %
                 (mapper_ == 28U ? 0x8000U : (mapper_ == 13U ? 0x4000U : 0x2000U))] = value;
}

MirrorMode DiscreteMapper::mirror_mode() const {
    return mirror_;
}

void DiscreteMapper::save_state(StateWriter& writer) const {
    if (mapper_ == 202U) {
        writer.write(kMapper202StateTag);
        writer.write(static_cast<Byte>(address_latch_));
        return;
    }
    writer.write_array(registers_);
    writer.write(address_latch_);
    writer.write(register_select_);
    writer.write_array(nibble_ram_);
    writer.write(mirror_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void DiscreteMapper::load_state(StateReader& reader) {
    if (mapper_ == 202U) {
        const auto tag = reader.read<uint16_t>();
        const auto latch = reader.read<Byte>();
        if (!reader.ok() || tag != kMapper202StateTag || latch > 15U) {
            reset();
            reader.fail();
            return;
        }
        address_latch_ = latch;
        mirror_ = (latch & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        return;
    }
    reader.read_array(registers_);
    reader.read(address_latch_);
    reader.read(register_select_);
    reader.read_array(nibble_ram_);
    reader.read(mirror_);
    reader.read_array(prg_ram_);
    reader.read_array(chr_ram_);
    if ((register_select_ & ~0x81U) != 0U || mirror_ > MirrorMode::SingleUpper ||
        std::ranges::any_of(nibble_ram_, [](Byte value) { return value > 15U; })) {
        mirror_ = initial_mirror_;
        reader.fail();
    }
}

std::span<const Byte> DiscreteMapper::battery_ram() const {
    return has_prg_ram() ? std::span<const Byte>(prg_ram_) : std::span<const Byte>{};
}

void DiscreteMapper::set_battery_ram(std::span<const Byte> data) {
    if (has_prg_ram())
        std::copy_n(data.begin(), std::min(data.size(), prg_ram_.size()), prg_ram_.begin());
}

} // namespace mapperbus::core
