#include "core/mappers/mapper357.hpp"

#include <array>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kSmbPrgBankSize = 0x2000UZ;
constexpr std::size_t kUnromPrgBankSize = 0x4000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3570U;
constexpr std::uint16_t kIrqCounterMask = 0x0FFFU;
constexpr Byte kIrqEnabled = 1U;
constexpr Byte kIrqPending = 2U;
constexpr std::array<Byte, 8> kSmbBanks{4U, 3U, 5U, 3U, 6U, 3U, 7U, 3U};

} // namespace

// NESdev mapper 357 oldid 7940 defines the DIP-selected modes, memory map and
// tentative register masks. Zeroed registers, cold CHR-RAM clearing and RAM-
// retaining soft reset are deterministic policies; electrical defaults are not
// established by the article. The board has no CPU RAM or expansion audio.
Mapper357::Mapper357(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper357::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 357U) {
        return std::unexpected("Mapper 357 requires mapper ID 357");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 357 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize) {
        return std::unexpected("Mapper 357 requires exactly 512 KiB PRG ROM");
    }
    if (chr_size != 0U) {
        return std::unexpected("Mapper 357 requires no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 357 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 357 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper357::read_prg(Address addr) {
    if (dip_switches_ == 0U) {
        unsigned bank = 0U;
        if (addr >= 0x5000U && addr < 0x6000U) {
            bank = 16U;
        } else if (addr >= 0x6000U && addr < 0x8000U) {
            bank = registers_.smb_fixed_banks == 0U ? 2U : 0U;
        } else if (addr >= 0x8000U) {
            switch ((addr - 0x8000U) >> 13U) {
            case 0U:
                bank = 1U;
                break;
            case 1U:
                bank = 0U;
                break;
            case 2U:
                bank = kSmbBanks[registers_.smb_bank];
                break;
            default:
                bank = registers_.smb_fixed_banks == 0U ? 10U : 8U;
                break;
            }
        } else {
            return 0U;
        }
        const auto offset = static_cast<std::size_t>(bank) * kSmbPrgBankSize;
        const auto inner = addr < 0x6000U ? addr & 0x0FFFU : addr & 0x1FFFU;
        return prg_rom_[offset + inner];
    }

    if (addr < 0x8000U) {
        return 0U;
    }
    const auto outer_bank = static_cast<unsigned>(dip_switches_) * 8U;
    const unsigned inner_bank = addr < 0xC000U ? registers_.unrom_bank : 7U;
    const auto offset = static_cast<std::size_t>(outer_bank + inner_bank) * kUnromPrgBankSize;
    return prg_rom_[offset + (addr & 0x3FFFU)];
}

void Mapper357::write_prg(Address addr, Byte value) {
    if (dip_switches_ != 0U && addr >= 0x8000U) {
        registers_.unrom_bank = value & 7U;
    }
}

bool Mapper357::maps_prg(Address addr) const {
    return addr >= 0x8000U || (dip_switches_ == 0U && addr >= 0x5000U && addr < 0x8000U);
}

Byte Mapper357::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper357::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper357::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper357::mirror_mode() const {
    return dip_switches_ == 3U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper357::write_expansion(Address addr, Byte value) {
    if (dip_switches_ != 0U || addr < 0x4020U || addr >= 0x6000U) {
        return;
    }
    if ((addr & 0x71FFU) == 0x4022U) {
        registers_.smb_bank = value & 7U;
    } else if ((addr & 0x71FFU) == 0x4120U) {
        registers_.smb_fixed_banks = value & 1U;
    } else if ((addr & 0xF1FFU) == 0x4122U) {
        if ((value & 1U) != 0U) {
            registers_.irq_flags |= kIrqEnabled;
        } else {
            registers_.irq_flags = 0U;
            registers_.irq_counter = 0U;
        }
    }
}

Result<void> Mapper357::configure_board_jumpers(Byte value) {
    if (value > 3U) {
        return std::unexpected("Mapper 357 DIP switches accept game selections 0 through 3");
    }
    dip_switches_ = value;
    return {};
}

void Mapper357::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper357::reset() {
    registers_ = Registers{};
}

bool Mapper357::irq_pending() const {
    return (registers_.irq_flags & kIrqPending) != 0U;
}

void Mapper357::clock_cpu() {
    if (dip_switches_ != 0U || (registers_.irq_flags & kIrqEnabled) == 0U) {
        return;
    }
    if (registers_.irq_counter == kIrqCounterMask) {
        registers_.irq_counter = 0U;
        registers_.irq_flags |= kIrqPending;
    } else {
        ++registers_.irq_counter;
    }
}

std::uint16_t Mapper357::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | dip_switches_);
}

void Mapper357::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.smb_bank);
    writer.write(registers_.smb_fixed_banks);
    writer.write(registers_.unrom_bank);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_flags);
    writer.write_array(chr_ram_);
}

void Mapper357::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.smb_bank);
    reader.read(next.smb_fixed_banks);
    reader.read(next.unrom_bank);
    reader.read(next.irq_counter);
    reader.read(next.irq_flags);
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid_irq =
        next.irq_counter <= kIrqCounterMask && next.irq_flags <= 3U &&
        ((next.irq_flags & kIrqPending) == 0U || (next.irq_flags & kIrqEnabled) != 0U) &&
        ((next.irq_flags & kIrqEnabled) != 0U || (next.irq_flags == 0U && next.irq_counter == 0U));
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        next.smb_bank > 7U || next.smb_fixed_banks > 1U || next.unrom_bank > 7U || !valid_irq) {
        reader.fail();
        return;
    }

    registers_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
