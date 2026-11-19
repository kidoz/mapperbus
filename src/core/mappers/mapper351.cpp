#include "core/mappers/mapper351.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgPage = 0x2000UZ;
constexpr std::size_t kChrPage = 0x0400UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3510U;

constexpr MirrorMode vrc_mirror(Byte value) {
    constexpr std::array modes{MirrorMode::Vertical,
                               MirrorMode::Horizontal,
                               MirrorMode::SingleLower,
                               MirrorMode::SingleUpper};
    return modes[value & 3U];
}

} // namespace

// Board decoding follows NESdev mapper 351 oldid 19719 and the public MMC1,
// MMC3 and VRC4 register definitions. Outer bases are byte addresses in their
// documented 16/8 KiB units; the selected inner span is added to that base.
// Fitted ROMs wrap after decoding. The article does not define electrical reset
// values beyond the three outer registers, so reset uses ordinary ASIC defaults
// and header H/V mirroring. Cold CHR-RAM clearing is a deterministic policy.
Mapper351::Mapper351(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper351::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 351U) {
        return std::unexpected("Mapper 351 requires mapper ID 351");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 351 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < 0x4000U || prg_size > kMaxPrgSize) {
        return std::unexpected("Mapper 351 requires power-of-two 16-1024 KiB PRG ROM");
    }
    if (!std::has_single_bit(chr_size) || chr_size < 0x2000U || chr_size > kMaxChrSize) {
        return std::unexpected("Mapper 351 requires power-of-two 8-512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 351 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 351 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Mapper351::Asic Mapper351::asic() const {
    switch (registers_.chr_base & 3U) {
    case 2U:
        return Asic::Mmc1;
    case 3U:
        return Asic::Vrc4;
    default:
        return Asic::Mmc3;
    }
}

std::size_t Mapper351::prg_span() const {
    if ((registers_.banking & 0x10U) != 0U) {
        switch ((registers_.banking >> 2U) & 3U) {
        case 0U:
            return 0x8000UZ;
        case 1U:
            return 0x4000UZ;
        default:
            return 0x2000UZ;
        }
    }
    // The source assigns 256/128 KiB spans to ASIC modes 0/1. Its 8 KiB entry
    // is explicitly NROM-only; treating the remaining ASIC encodings as one
    // mirrored page is a bounded compatibility policy, not a hardware claim.
    return (registers_.banking & 4U) != 0U   ? 0x2000UZ
           : (registers_.banking & 8U) != 0U ? 0x20000UZ
                                             : 0x40000UZ;
}

std::size_t Mapper351::chr_span() const {
    if ((registers_.banking & 0x80U) != 0U) {
        return 0x2000UZ;
    }
    if ((registers_.banking & 0x40U) != 0U) {
        return 0x20000UZ;
    }
    return (registers_.banking & 0x20U) != 0U ? 0x8000UZ : 0x40000UZ;
}

std::size_t Mapper351::prg_inner_offset(Address addr) const {
    const auto span = prg_span();
    if ((registers_.banking & 0x10U) != 0U) {
        return addr & 0x7FFFU;
    }

    switch (asic()) {
    case Asic::Mmc1: {
        const auto count = std::max(1UZ, span / 0x4000UZ);
        const auto bank = static_cast<std::size_t>(registers_.mmc1_prg & 0x0FU);
        std::size_t selected = 0U;
        switch ((registers_.mmc1_control >> 2U) & 3U) {
        case 0U:
        case 1U:
            selected = ((bank & 0x0EU) % count) * 0x4000UZ + (addr & 0x7FFFU);
            break;
        case 2U:
            selected =
                addr < 0xC000U ? addr & 0x3FFFU : (bank % count) * 0x4000UZ + (addr & 0x3FFFU);
            break;
        default:
            selected = addr < 0xC000U ? (bank % count) * 0x4000UZ + (addr & 0x3FFFU)
                                      : (count - 1U) * 0x4000UZ + (addr & 0x3FFFU);
            break;
        }
        return selected;
    }
    case Asic::Vrc4: {
        const auto count = std::max(1UZ, span / kPrgPage);
        const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
        std::size_t bank = count - 1U;
        if (slot == 1U) {
            bank = registers_.vrc_prg[1] % count;
        } else if (slot != 3U) {
            const bool swap = (registers_.vrc_mode & 2U) != 0U;
            bank = ((slot == 0U) != swap) ? registers_.vrc_prg[0] % count
                                          : (count > 1U ? count - 2U : 0U);
        }
        return bank * kPrgPage + (addr & 0x1FFFU);
    }
    default: {
        const auto count = std::max(1UZ, span / kPrgPage);
        const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
        const auto r6 = static_cast<std::size_t>(registers_.mmc3_banks[6] & 0x3FU) % count;
        const auto r7 = static_cast<std::size_t>(registers_.mmc3_banks[7] & 0x3FU) % count;
        const auto penultimate = count > 1U ? count - 2U : 0U;
        std::size_t bank = 0U;
        if ((registers_.mmc3_select & 0x40U) == 0U) {
            constexpr std::array<unsigned, 4> fixed{0U, 1U, 2U, 3U};
            switch (fixed[slot]) {
            case 0U:
                bank = r6;
                break;
            case 1U:
                bank = r7;
                break;
            case 2U:
                bank = penultimate;
                break;
            default:
                bank = count - 1U;
                break;
            }
        } else {
            switch (slot) {
            case 0U:
                bank = penultimate;
                break;
            case 1U:
                bank = r7;
                break;
            case 2U:
                bank = r6;
                break;
            default:
                bank = count - 1U;
                break;
            }
        }
        return bank * kPrgPage + (addr & 0x1FFFU);
    }
    }
}

std::size_t Mapper351::chr_inner_offset(Address addr) const {
    if ((registers_.banking & 0x10U) != 0U) {
        return addr & 0x1FFFU;
    }

    switch (asic()) {
    case Asic::Mmc1:
        if ((registers_.mmc1_control & 0x10U) != 0U) {
            return addr < 0x1000U ? static_cast<std::size_t>(registers_.mmc1_chr0) * 0x1000UZ + addr
                                  : static_cast<std::size_t>(registers_.mmc1_chr1) * 0x1000UZ +
                                        (addr - 0x1000U);
        }
        return static_cast<std::size_t>(registers_.mmc1_chr0 & 0x1EU) * 0x1000UZ + addr;
    case Asic::Vrc4:
        return static_cast<std::size_t>(registers_.vrc_chr[(addr >> 10U) & 7U]) * kChrPage +
               (addr & 0x03FFU);
    default: {
        auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
        if ((registers_.mmc3_select & 0x80U) != 0U) {
            slot ^= 4U;
        }
        const auto bank =
            slot < 4U
                ? static_cast<unsigned>((registers_.mmc3_banks[slot / 2U] & 0xFEU) | (slot & 1U))
                : static_cast<unsigned>(registers_.mmc3_banks[slot - 2U]);
        return bank * kChrPage + (addr & 0x03FFU);
    }
    }
}

Byte Mapper351::read_combined_prg(std::size_t offset) const {
    // In CHR-RAM mode the source describes one enlarged PRG address space.
    // Append CHR ROM at the physical PRG extent; equal-sized chips therefore
    // form the documented two halves, without duplicating either ROM vector.
    const auto total = prg_rom_.size() + chr_rom_.size();
    offset %= total;
    if (offset < prg_rom_.size()) {
        return prg_rom_[offset];
    }
    return chr_rom_[offset - prg_rom_.size()];
}

Byte Mapper351::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto inner = prg_inner_offset(addr) & (prg_span() - 1U);
    const auto offset = static_cast<std::size_t>(registers_.prg_base >> 2U) * 0x4000UZ + inner;
    if ((registers_.banking & 1U) != 0U) {
        return read_combined_prg(offset);
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper351::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (addr < 0x8000U || (registers_.banking & 0x10U) != 0U || asic() != Asic::Mmc1 ||
        !consecutive || (value & 0x80U) != 0U) {
        write_prg(addr, value);
    }
}

void Mapper351::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U || (registers_.banking & 0x10U) != 0U) {
        return;
    }

    switch (asic()) {
    case Asic::Mmc1:
        write_mmc1(addr, value);
        return;
    case Asic::Vrc4:
        write_vrc4(addr, value);
        return;
    default:
        switch (addr & 0xE001U) {
        case 0x8000U:
            registers_.mmc3_select = value & 0xC7U;
            break;
        case 0x8001U:
            registers_.mmc3_banks[registers_.mmc3_select & 7U] =
                (registers_.mmc3_select & 7U) >= 6U ? value & 0x3FU : value;
            break;
        case 0xA000U:
            registers_.mmc3_mirror = value & 1U;
            break;
        case 0xC000U:
            registers_.mmc3_irq_reload = value;
            break;
        case 0xC001U:
            registers_.mmc3_irq_counter = 0U;
            registers_.mmc3_irq_flags |= 1U;
            break;
        case 0xE000U:
            registers_.mmc3_irq_flags &= 1U;
            break;
        case 0xE001U:
            registers_.mmc3_irq_flags |= 2U;
            break;
        default:
            break;
        }
        return;
    }
}

void Mapper351::write_mmc1(Address addr, Byte value) {
    if ((value & 0x80U) != 0U) {
        registers_.mmc1_shift = 0x10U;
        registers_.mmc1_count = 0U;
        registers_.mmc1_control |= 0x0CU;
        return;
    }
    registers_.mmc1_shift >>= 1U;
    registers_.mmc1_shift |= static_cast<Byte>((value & 1U) << 4U);
    ++registers_.mmc1_count;
    if (registers_.mmc1_count == 5U) {
        write_mmc1_register(addr, registers_.mmc1_shift);
        registers_.mmc1_shift = 0x10U;
        registers_.mmc1_count = 0U;
    }
}

void Mapper351::write_mmc1_register(Address addr, Byte value) {
    if (addr < 0xA000U) {
        registers_.mmc1_control = value & 0x1FU;
    } else if (addr < 0xC000U) {
        registers_.mmc1_chr0 = value & 0x1FU;
    } else if (addr < 0xE000U) {
        registers_.mmc1_chr1 = value & 0x1FU;
    } else {
        registers_.mmc1_prg = value & 0x1FU;
    }
}

void Mapper351::write_vrc4(Address addr, Byte value) {
    const auto group = static_cast<unsigned>(addr) >> 12U;
    const auto index = (addr & 0x0800U) == 0U
                           ? static_cast<unsigned>((addr >> 2U) & 3U)
                           : static_cast<unsigned>(((addr >> 3U) & 1U) | ((addr >> 1U) & 2U));
    if (group == 8U || group == 10U) {
        registers_.vrc_prg[group == 8U ? 0U : 1U] = value & 0x1FU;
    } else if (group == 9U) {
        if (index == 0U) {
            registers_.vrc_mirror = value & 3U;
        } else if (index == 2U) {
            registers_.vrc_mode = value & 2U;
        }
    } else if (group >= 11U && group <= 14U) {
        auto& bank = registers_.vrc_chr[(group - 11U) * 2U + (index >> 1U)];
        if ((index & 1U) == 0U) {
            bank = (bank & 0x1F0U) | (value & 0x0FU);
        } else {
            bank = (bank & 0x00FU) | static_cast<std::uint16_t>((value & 0x1FU) << 4U);
        }
    } else if (group == 15U) {
        switch (index) {
        case 0U:
            registers_.vrc_irq_latch = (registers_.vrc_irq_latch & 0xF0U) | (value & 0x0FU);
            break;
        case 1U:
            registers_.vrc_irq_latch =
                static_cast<Byte>((registers_.vrc_irq_latch & 0x0FU) | ((value & 0x0FU) << 4U));
            break;
        case 2U:
            vrc_irq_control(value);
            break;
        case 3U:
            registers_.vrc_irq_pending = 0U;
            registers_.vrc_irq_enabled = registers_.vrc_irq_control & 1U;
            break;
        default:
            break;
        }
    }
}

Byte Mapper351::read_chr(Address addr) {
    addr &= 0x1FFFU;
    if ((registers_.banking & 1U) != 0U) {
        return chr_ram_[addr];
    }
    const auto inner = chr_inner_offset(addr) & (chr_span() - 1U);
    const auto offset = static_cast<std::size_t>(registers_.chr_base >> 2U) * 0x2000UZ + inner;
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper351::write_chr(Address addr, Byte value) {
    if ((registers_.banking & 1U) != 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper351::mirror_mode() const {
    if ((registers_.banking & 0x10U) != 0U) {
        return registers_.nrom_mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    }
    switch (asic()) {
    case Asic::Mmc1:
        switch (registers_.mmc1_control & 3U) {
        case 0U:
            return MirrorMode::SingleLower;
        case 1U:
            return MirrorMode::SingleUpper;
        case 2U:
            return MirrorMode::Vertical;
        default:
            return MirrorMode::Horizontal;
        }
    case Asic::Vrc4:
        return vrc_mirror(registers_.vrc_mirror);
    default:
        return registers_.mmc3_mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    }
}

bool Mapper351::maps_expansion(Address addr) const {
    return addr == 0x4025U || (addr & 0xF003U) == 0x5000U || (addr & 0xF003U) == 0x5001U ||
           (addr & 0xF003U) == 0x5002U;
}

Byte Mapper351::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0U : 0xFFU;
}

Byte Mapper351::read_expansion(Address) {
    return 0U;
}

void Mapper351::write_expansion(Address addr, Byte value) {
    if (addr == 0x4025U) {
        if ((registers_.banking & 0x10U) != 0U) {
            registers_.nrom_mirror = (value >> 3U) & 1U;
        }
        return;
    }
    switch (addr & 0xF003U) {
    case 0x5000U:
        registers_.chr_base = value;
        break;
    case 0x5001U:
        registers_.prg_base = value & 0xFCU;
        break;
    case 0x5002U:
        // The bit diagram marks D7 unused, but the following explicit CHR-size
        // table assigns 000/001/01x/1xx. Preserve D7 and follow that table.
        registers_.banking = value & 0xFDU;
        break;
    default:
        break;
    }
}

void Mapper351::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper351::reset() {
    registers_ = Registers{};
    registers_.nrom_mirror = initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
    registers_.mmc3_mirror = registers_.nrom_mirror;
    registers_.vrc_mirror = registers_.nrom_mirror;
}

bool Mapper351::irq_pending() const {
    if ((registers_.banking & 0x10U) != 0U) {
        return false;
    }
    if (asic() == Asic::Mmc3) {
        return (registers_.mmc3_irq_flags & 4U) != 0U;
    }
    return asic() == Asic::Vrc4 && registers_.vrc_irq_pending != 0U;
}

void Mapper351::acknowledge_irq() {
    if (asic() == Asic::Mmc3) {
        registers_.mmc3_irq_flags &= 3U;
    } else if (asic() == Asic::Vrc4) {
        registers_.vrc_irq_pending = 0U;
    }
}

void Mapper351::clock_irq_counter() {
    if ((registers_.banking & 0x10U) != 0U || asic() != Asic::Mmc3) {
        return;
    }
    if (registers_.mmc3_irq_counter == 0U || (registers_.mmc3_irq_flags & 1U) != 0U) {
        registers_.mmc3_irq_counter = registers_.mmc3_irq_reload;
        registers_.mmc3_irq_flags &= 6U;
    } else {
        --registers_.mmc3_irq_counter;
    }
    if (registers_.mmc3_irq_counter == 0U && (registers_.mmc3_irq_flags & 2U) != 0U) {
        registers_.mmc3_irq_flags |= 4U;
    }
}

void Mapper351::vrc_irq_control(Byte value) {
    registers_.vrc_irq_control = value & 7U;
    registers_.vrc_irq_enabled = (value >> 1U) & 1U;
    registers_.vrc_irq_pending = 0U;
    if (registers_.vrc_irq_enabled != 0U) {
        registers_.vrc_irq_counter = registers_.vrc_irq_latch;
        registers_.vrc_irq_prescaler = 341U;
    }
}

void Mapper351::vrc_irq_tick() {
    if (registers_.vrc_irq_counter == 0xFFU) {
        registers_.vrc_irq_counter = registers_.vrc_irq_latch;
        registers_.vrc_irq_pending = 1U;
    } else {
        ++registers_.vrc_irq_counter;
    }
}

void Mapper351::clock_cpu() {
    if ((registers_.banking & 0x10U) != 0U || asic() != Asic::Vrc4 ||
        registers_.vrc_irq_enabled == 0U) {
        return;
    }
    if ((registers_.vrc_irq_control & 4U) != 0U) {
        vrc_irq_tick();
    } else if (registers_.vrc_irq_prescaler <= 3U) {
        registers_.vrc_irq_prescaler += 338U;
        vrc_irq_tick();
    } else {
        registers_.vrc_irq_prescaler -= 3U;
    }
}

std::uint16_t Mapper351::state_tag() const {
    return kStateTag | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U);
}

void Mapper351::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.chr_base);
    writer.write(registers_.prg_base);
    writer.write(registers_.banking);
    writer.write(registers_.nrom_mirror);
    writer.write(registers_.mmc3_select);
    writer.write_array(registers_.mmc3_banks);
    writer.write(registers_.mmc3_mirror);
    writer.write(registers_.mmc3_irq_counter);
    writer.write(registers_.mmc3_irq_reload);
    writer.write(registers_.mmc3_irq_flags);
    writer.write(registers_.mmc1_shift);
    writer.write(registers_.mmc1_count);
    writer.write(registers_.mmc1_control);
    writer.write(registers_.mmc1_chr0);
    writer.write(registers_.mmc1_chr1);
    writer.write(registers_.mmc1_prg);
    writer.write_array(registers_.vrc_prg);
    writer.write_array(registers_.vrc_chr);
    writer.write(registers_.vrc_mirror);
    writer.write(registers_.vrc_mode);
    writer.write(registers_.vrc_irq_latch);
    writer.write(registers_.vrc_irq_counter);
    writer.write(registers_.vrc_irq_prescaler);
    writer.write(registers_.vrc_irq_control);
    writer.write(registers_.vrc_irq_enabled);
    writer.write(registers_.vrc_irq_pending);
    writer.write_array(chr_ram_);
}

void Mapper351::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.chr_base);
    reader.read(next.prg_base);
    reader.read(next.banking);
    reader.read(next.nrom_mirror);
    reader.read(next.mmc3_select);
    reader.read_array(next.mmc3_banks);
    reader.read(next.mmc3_mirror);
    reader.read(next.mmc3_irq_counter);
    reader.read(next.mmc3_irq_reload);
    reader.read(next.mmc3_irq_flags);
    reader.read(next.mmc1_shift);
    reader.read(next.mmc1_count);
    reader.read(next.mmc1_control);
    reader.read(next.mmc1_chr0);
    reader.read(next.mmc1_chr1);
    reader.read(next.mmc1_prg);
    reader.read_array(next.vrc_prg);
    reader.read_array(next.vrc_chr);
    reader.read(next.vrc_mirror);
    reader.read(next.vrc_mode);
    reader.read(next.vrc_irq_latch);
    reader.read(next.vrc_irq_counter);
    reader.read(next.vrc_irq_prescaler);
    reader.read(next.vrc_irq_control);
    reader.read(next.vrc_irq_enabled);
    reader.read(next.vrc_irq_pending);
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid_mmc3 = (next.mmc3_select & 0x38U) == 0U && next.mmc3_banks[6] <= 0x3FU &&
                            next.mmc3_banks[7] <= 0x3FU && next.mmc3_mirror <= 1U &&
                            next.mmc3_irq_flags <= 7U &&
                            ((next.mmc3_irq_flags & 4U) == 0U || (next.mmc3_irq_flags & 2U) != 0U);
    const bool valid_mmc1 = next.mmc1_shift <= 0x1FU && next.mmc1_count <= 4U &&
                            next.mmc1_control <= 0x1FU && next.mmc1_chr0 <= 0x1FU &&
                            next.mmc1_chr1 <= 0x1FU && next.mmc1_prg <= 0x1FU;
    const bool valid_vrc =
        next.vrc_prg[0] <= 0x1FU && next.vrc_prg[1] <= 0x1FU &&
        std::ranges::all_of(next.vrc_chr, [](std::uint16_t bank) { return bank <= 511U; }) &&
        next.vrc_mirror <= 3U && (next.vrc_mode & ~2U) == 0U && next.vrc_irq_prescaler >= 1U &&
        next.vrc_irq_prescaler <= 341U && next.vrc_irq_control <= 7U &&
        next.vrc_irq_enabled <= 1U && next.vrc_irq_pending <= 1U &&
        (next.vrc_irq_pending == 0U || next.vrc_irq_enabled != 0U);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (next.prg_base & 3U) != 0U || (next.banking & 2U) != 0U ||
        next.nrom_mirror > 1U || !valid_mmc3 || !valid_mmc1 || !valid_vrc) {
        reader.fail();
        return;
    }

    registers_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
