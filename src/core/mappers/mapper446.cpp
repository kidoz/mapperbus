#include "core/mappers/mapper446.hpp"

#include <algorithm>
#include <utility>

#include "core/apu/expansion_gains.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x1000000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kPrgRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4460U;
constexpr Byte kSupervisorFlagsMask = 0xC1U;
constexpr Byte kChrProtectMask = 0x04U;
constexpr Byte kOuterChrMask = 0x1FU;

constexpr MirrorMode vrc_mirror(Byte value) {
    constexpr std::array modes{MirrorMode::Vertical,
                               MirrorMode::Horizontal,
                               MirrorMode::SingleLower,
                               MirrorMode::SingleUpper};
    return modes[value & 3U];
}

} // namespace

// NESdev mapper 446 oldid 22849 does not specify electrical reset values.
// The first listed inner mapper, ordinary ASIC defaults and cleared supervisor
// registers are deterministic reset policy. Cold RAM clearing is also policy.
Mapper446::Mapper446(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)), chr_ram_(header.chr_ram_size), submapper_(header.submapper),
      initial_mirror_(header.mirror_mode),
      prg_ram_size_(header.prg_ram_size + header.prg_nvram_size),
      nvram_size_(header.prg_nvram_size) {
    power_on();
}

Result<void> Mapper446::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 446U) {
        return std::unexpected("Mapper 446 requires mapper ID 446");
    }
    if (!header.is_nes2 || header.submapper > 3U) {
        return std::unexpected("Mapper 446 requires NES 2.0 submapper 0 through 3");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 446 requires 32 KiB-16 MiB PRG ROM in 8 KiB units");
    }
    if (chr_size != 0U ||
        (header.chr_ram_size != 0x008000UZ && header.chr_ram_size != 0x020000UZ &&
         header.chr_ram_size != 0x040000UZ) ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 446 requires no CHR ROM/NVRAM and 32, 128 or 256 KiB volatile CHR RAM");
    }
    if ((header.prg_ram_size != 0U && header.prg_ram_size != kPrgRamSize) ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kPrgRamSize) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
        header.has_battery != (header.prg_nvram_size != 0U)) {
        return std::unexpected("Mapper 446 supports no CPU RAM or one 8 KiB PRG RAM/NVRAM chip");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 446 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 446 does not provide trainer RAM");
    }
    return {};
}

std::optional<Mapper446::Inner> Mapper446::inner() const {
    const Byte code = registers_.selection & 0x1FU;
    if (submapper_ == 0U) {
        return code == 1U ? std::optional<Inner>{Inner::Skrom} : std::nullopt;
    }
    if (submapper_ == 3U) {
        return code == 1U ? std::optional<Inner>{Inner::H3001} : std::nullopt;
    }
    switch (code) {
    case 0x00U:
        return Inner::Unrom;
    case 0x01U:
        return Inner::Tkrom;
    case 0x02U:
        return Inner::Bnrom;
    case 0x03U:
        return Inner::Cnrom;
    case 0x04U:
        return Inner::Anrom;
    case 0x05U:
        return Inner::Skrom;
    case 0x06U:
        return Inner::Snrom;
    case 0x07U:
        return Inner::Surom;
    case 0x08U:
        return Inner::Gnrom;
    case 0x09U:
        return submapper_ == 1U ? Inner::Pnrom : Inner::Tlrom;
    case 0x0AU:
        return Inner::Hkrom;
    case 0x0BU:
        return Inner::Bandai152;
    case 0x0DU:
        return submapper_ == 2U ? std::optional<Inner>{Inner::Tkrom} : std::nullopt;
    case 0x0EU:
        return Inner::Tksrom;
    case 0x0FU:
        return Inner::Mapper189;
    case 0x10U:
        return Inner::Vrc6;
    case 0x12U:
        return Inner::Vrc2;
    case 0x15U:
        return Inner::Vrc4;
    case 0x18U:
        return Inner::Vrc23;
    case 0x1AU:
        return Inner::Vrc1;
    default:
        return std::nullopt;
    }
}

bool Mapper446::is_mmc1() const {
    const auto selected = inner();
    return selected == Inner::Skrom || selected == Inner::Snrom || selected == Inner::Surom;
}

bool Mapper446::is_mmc3() const {
    const auto selected = inner();
    return selected == Inner::Tkrom || selected == Inner::Tlrom || selected == Inner::Tksrom ||
           selected == Inner::Mapper189;
}

bool Mapper446::is_vrc() const {
    const auto selected = inner();
    return selected == Inner::Vrc6 || selected == Inner::Vrc2 || selected == Inner::Vrc4 ||
           selected == Inner::Vrc23 || selected == Inner::Vrc1;
}

bool Mapper446::ram_enabled(Address addr) const {
    if (prg_ram_size_ == 0U || addr < 0x6000U || addr >= 0x8000U) {
        return false;
    }
    const auto selected = inner();
    if (!selected) {
        return false;
    }
    if (is_mmc1()) {
        return (registers_.mmc1_prg & 0x10U) == 0U;
    }
    if (is_mmc3()) {
        return (registers_.mmc3_ram_control & 0x80U) != 0U;
    }
    if (*selected == Inner::Vrc4 || *selected == Inner::Vrc23) {
        return (registers_.vrc_mode & 1U) != 0U;
    }
    return *selected == Inner::Pnrom || *selected == Inner::Hkrom || *selected == Inner::Vrc6 ||
           *selected == Inner::Vrc2 || *selected == Inner::H3001;
}

bool Mapper446::ram_writable() const {
    return !is_mmc3() || (registers_.mmc3_ram_control & 0x40U) == 0U;
}

bool Mapper446::maps_prg(Address addr) const {
    if (addr >= 0x8000U) {
        return inner().has_value();
    }
    if (inner() == Inner::Vrc2 && prg_ram_size_ == 0U && addr >= 0x6000U && addr < 0x7000U) {
        return true;
    }
    return ram_enabled(addr);
}

Byte Mapper446::prg_read_mask(Address addr) const {
    if (inner() == Inner::Vrc2 && prg_ram_size_ == 0U && addr >= 0x6000U && addr < 0x7000U) {
        return 1U;
    }
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper446::inner_prg_offset(Address addr) const {
    const auto selected = inner().value();
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    unsigned bank = 0U;
    switch (selected) {
    case Inner::Unrom:
        bank = slot < 2U ? (registers_.discrete_bank & 7U) * 2U + slot : 14U + slot - 2U;
        break;
    case Inner::Bnrom:
    case Inner::Anrom:
        bank = (registers_.discrete_bank & 7U) * 4U + slot;
        break;
    case Inner::Cnrom:
        bank = slot;
        break;
    case Inner::Skrom:
    case Inner::Snrom:
    case Inner::Surom: {
        unsigned selected_bank = registers_.mmc1_prg & 0x0FU;
        const unsigned last = selected == Inner::Surom ? 31U : 15U;
        if (selected == Inner::Surom) {
            selected_bank |= registers_.mmc1_chr0 & 0x10U;
        }
        unsigned bank16 = 0U;
        switch ((registers_.mmc1_control >> 2U) & 3U) {
        case 0U:
        case 1U:
            bank16 = (selected_bank & 0x0EU) | ((addr >> 14U) & 1U);
            break;
        case 2U:
            bank16 = addr < 0xC000U ? 0U : selected_bank;
            break;
        default:
            bank16 = addr < 0xC000U ? selected_bank : last;
            break;
        }
        bank = bank16 * 2U + ((addr >> 13U) & 1U);
        break;
    }
    case Inner::Gnrom:
        bank = ((registers_.discrete_bank >> 4U) & 3U) * 4U + slot;
        break;
    case Inner::Pnrom:
        bank = slot == 0U ? registers_.mmc2_prg & 0x0FU : 13U + slot - 1U;
        break;
    case Inner::Tkrom:
    case Inner::Tlrom:
    case Inner::Tksrom:
        if (slot == 1U) {
            bank = registers_.mmc3_banks[7U] & 0x3FU;
        } else if (((registers_.mmc3_select & 0x40U) == 0U && slot == 0U) ||
                   ((registers_.mmc3_select & 0x40U) != 0U && slot == 2U)) {
            bank = registers_.mmc3_banks[6U] & 0x3FU;
        } else {
            bank = slot == 3U ? 31U : 30U;
        }
        break;
    case Inner::Hkrom:
        bank = slot < 2U ? (registers_.mmc2_prg & 0x0FU) * 2U + slot : 30U + slot - 2U;
        break;
    case Inner::Bandai152:
        bank = slot < 2U ? ((registers_.discrete_bank >> 4U) & 7U) * 2U + slot : 14U + slot - 2U;
        break;
    case Inner::Mapper189:
        bank = (registers_.discrete_bank & 0x0FU) * 4U + slot;
        break;
    case Inner::Vrc6:
        if (slot < 2U) {
            bank = (registers_.vrc_prg[0U] & 0x0FU) * 2U + slot;
        } else {
            bank = slot == 2U ? registers_.vrc_prg[1U] & 0x1FU : 31U;
        }
        break;
    case Inner::Vrc2:
    case Inner::Vrc4:
    case Inner::Vrc23:
        if (slot == 1U) {
            bank = registers_.vrc_prg[1U] & 0x1FU;
        } else if (slot == 3U) {
            bank = 31U;
        } else {
            const bool swap = selected != Inner::Vrc2 && (registers_.vrc_mode & 2U) != 0U;
            bank = (slot == 0U) != swap ? registers_.vrc_prg[0U] & 0x1FU : 30U;
        }
        break;
    case Inner::Vrc1:
        bank = slot < 3U ? registers_.vrc_prg[slot] & 0x0FU : 15U;
        break;
    case Inner::H3001:
        if (slot == 1U) {
            bank = registers_.h3001_prg[1U] & 0x3FU;
        } else if (slot == 3U) {
            bank = 63U;
        } else {
            const bool switched = slot == (registers_.h3001_swap != 0U ? 2U : 0U);
            bank = switched ? registers_.h3001_prg[0U] & 0x3FU : 62U;
        }
        break;
    }
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

std::size_t Mapper446::outer_prg_offset(Address addr) const {
    const auto inner_offset = inner_prg_offset(addr);
    const unsigned inner_bank = static_cast<unsigned>(inner_offset / kPrgBankSize);
    const unsigned outer_bank = registers_.outer_prg_lsb | (registers_.outer_prg_msb << 8U);
    const unsigned mask = registers_.prg_mask;
    const unsigned bank = submapper_ == 2U ? (inner_bank & mask) | (outer_bank & ~mask)
                                           : (inner_bank & ~mask) | (outer_bank & mask);
    return bank * kPrgBankSize + (inner_offset & 0x1FFFU);
}

Byte Mapper446::read_prg(Address addr) {
    if (addr < 0x8000U) {
        if (ram_enabled(addr)) {
            return prg_ram_[addr & 0x1FFFU];
        }
        if (inner() == Inner::Vrc2 && prg_ram_size_ == 0U && addr >= 0x6000U && addr < 0x7000U) {
            return registers_.microwire;
        }
        return 0U;
    }
    if (!inner() || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[outer_prg_offset(addr) % prg_rom_.size()];
}

void Mapper446::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (inner() == Inner::Mapper189 && addr >= 0x4020U) {
            registers_.discrete_bank = static_cast<Byte>((value | (value >> 4U)) & 0x0FU);
        } else if (ram_enabled(addr)) {
            if (ram_writable()) {
                prg_ram_[addr & 0x1FFFU] = value;
            }
        } else if (inner() == Inner::Vrc2 && prg_ram_size_ == 0U && addr >= 0x6000U &&
                   addr < 0x7000U) {
            registers_.microwire = value & 1U;
        }
        return;
    }
    const auto selected = inner();
    if (!selected) {
        return;
    }
    if (is_mmc1()) {
        write_mmc1(addr, value);
        return;
    }
    if (is_mmc3()) {
        write_mmc3(addr, value);
        return;
    }
    switch (*selected) {
    case Inner::Unrom:
    case Inner::Bnrom:
    case Inner::Cnrom:
    case Inner::Anrom:
    case Inner::Gnrom:
    case Inner::Bandai152:
        registers_.discrete_bank = value;
        registers_.discrete_mirror = *selected == Inner::Anrom       ? value & 0x10U
                                     : *selected == Inner::Bandai152 ? value & 0x80U
                                                                     : registers_.discrete_mirror;
        break;
    case Inner::Pnrom:
    case Inner::Hkrom:
        switch (addr & 0xF000U) {
        case 0xA000U:
            registers_.mmc2_prg = value & 0x0FU;
            break;
        case 0xB000U:
        case 0xC000U:
        case 0xD000U:
        case 0xE000U:
            registers_.mmc2_chr[(addr - 0xB000U) >> 12U] = value & 0x1FU;
            break;
        case 0xF000U:
            if (*selected == Inner::Hkrom) {
                registers_.vrc_mirror = value & 1U;
            }
            break;
        default:
            break;
        }
        break;
    case Inner::Vrc6:
        write_vrc6(addr, value);
        break;
    case Inner::Vrc2:
    case Inner::Vrc4:
    case Inner::Vrc23:
    case Inner::Vrc1:
        write_vrc(addr, value);
        break;
    case Inner::H3001:
        write_h3001(addr, value);
        break;
    default:
        break;
    }
}

void Mapper446::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (!is_mmc1() || addr < 0x8000U || !consecutive || (value & 0x80U) != 0U) {
        write_prg(addr, value);
    }
}

unsigned Mapper446::mmc3_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((registers_.mmc3_select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.mmc3_banks[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.mmc3_banks[slot - 2U];
}

std::size_t Mapper446::inner_chr_offset(Address addr) const {
    const auto selected = inner();
    if (!selected) {
        return addr & 0x1FFFU;
    }
    addr &= 0x1FFFU;
    if (is_mmc1()) {
        const bool split = (registers_.mmc1_control & 0x10U) != 0U;
        const Byte bank = addr < 0x1000U ? registers_.mmc1_chr0 : registers_.mmc1_chr1;
        return (split ? bank : bank & 0x1EU) * 0x1000UZ + (addr & 0x0FFFU);
    }
    if (is_mmc3()) {
        return mmc3_chr_bank(addr) * 0x0400UZ + (addr & 0x03FFU);
    }
    switch (*selected) {
    case Inner::Cnrom:
        return (registers_.discrete_bank & 3U) * 0x2000UZ + addr;
    case Inner::Gnrom:
        return (registers_.discrete_bank & 3U) * 0x2000UZ + addr;
    case Inner::Bandai152:
        return (registers_.discrete_bank & 0x0FU) * 0x2000UZ + addr;
    case Inner::Pnrom:
    case Inner::Hkrom: {
        const unsigned half = addr >> 12U;
        const unsigned bank =
            registers_.mmc2_chr[half * 2U + (registers_.mmc2_latches[half] == 0U ? 0U : 2U)];
        return bank * 0x1000UZ + (addr & 0x0FFFU);
    }
    case Inner::Vrc6:
    case Inner::Vrc23:
    case Inner::Vrc4:
        return registers_.vrc_chr[addr >> 10U] * 0x0400UZ + (addr & 0x03FFU);
    case Inner::Vrc2:
        return (registers_.vrc_chr[addr >> 10U] >> 1U) * 0x0400UZ + (addr & 0x03FFU);
    case Inner::Vrc1:
        return registers_.vrc_chr[addr >> 12U] * 0x1000UZ + (addr & 0x0FFFU);
    case Inner::H3001:
        return registers_.h3001_chr[addr >> 10U] * 0x0400UZ + (addr & 0x03FFU);
    default:
        return addr;
    }
}

std::size_t Mapper446::outer_chr_offset(Address addr) const {
    const auto inner_offset = inner_chr_offset(addr);
    const unsigned inner_bank = static_cast<unsigned>(inner_offset / 0x2000UZ);
    const unsigned mask = (registers_.flags & 0x80U) == 0U   ? 0x1FU
                          : (registers_.flags & 0x40U) == 0U ? 0x0FU
                                                             : 0x03U;
    const unsigned bank = (registers_.outer_chr & ~mask) | (inner_bank & mask);
    return bank * 0x2000UZ + (inner_offset & 0x1FFFU);
}

Byte Mapper446::read_chr(Address addr) {
    if (chr_ram_.empty()) {
        return 0U;
    }
    addr &= 0x1FFFU;
    if (inner() == Inner::Pnrom) {
        if ((addr & 0x1FFFU) == 0x0FD8U || (addr & 0x1FFFU) == 0x1FD8U) {
            registers_.mmc2_latches[addr >> 12U] = 0U;
        } else if ((addr & 0x1FFFU) == 0x0FE8U || (addr & 0x1FFFU) == 0x1FE8U) {
            registers_.mmc2_latches[addr >> 12U] = 1U;
        }
    }
    const auto offset = outer_chr_offset(addr);
    const Byte value = chr_ram_[offset % chr_ram_.size()];
    if (inner() == Inner::Hkrom) {
        if ((addr & 0x0FF8U) == 0x0FD8U) {
            registers_.mmc2_latches[addr >> 12U] = 0U;
        } else if ((addr & 0x0FF8U) == 0x0FE8U) {
            registers_.mmc2_latches[addr >> 12U] = 1U;
        }
    }
    return value;
}

void Mapper446::write_chr(Address addr, Byte value) {
    if (!chr_ram_.empty() && (registers_.chr_protect & kChrProtectMask) == 0U) {
        chr_ram_[outer_chr_offset(addr) % chr_ram_.size()] = value;
    }
}

MirrorMode Mapper446::mirror_mode() const {
    const auto selected = inner();
    if (!selected) {
        return (registers_.flags & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    }
    if (is_mmc1()) {
        constexpr std::array modes{MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper,
                                   MirrorMode::Vertical,
                                   MirrorMode::Horizontal};
        return modes[registers_.mmc1_control & 3U];
    }
    if (*selected == Inner::Anrom) {
        return (registers_.discrete_mirror & 0x10U) != 0U ? MirrorMode::SingleUpper
                                                          : MirrorMode::SingleLower;
    }
    if (*selected == Inner::Bandai152) {
        return (registers_.discrete_mirror & 0x80U) != 0U ? MirrorMode::SingleUpper
                                                          : MirrorMode::SingleLower;
    }
    if (*selected == Inner::Tksrom) {
        const auto a = mmc3_chr_bank(0U) & 0x80U;
        const auto b = mmc3_chr_bank(0x400U) & 0x80U;
        const auto c = mmc3_chr_bank(0x800U) & 0x80U;
        const auto d = mmc3_chr_bank(0xC00U) & 0x80U;
        if (a == b && b == c && c == d) {
            return a != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
        }
        if (a == b && c == d) {
            return MirrorMode::Horizontal;
        }
        return MirrorMode::Vertical;
    }
    if (*selected == Inner::Tkrom || *selected == Inner::Tlrom || *selected == Inner::Mapper189) {
        return registers_.mmc3_mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    }
    if (*selected == Inner::Hkrom) {
        return registers_.vrc_mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    }
    if (*selected == Inner::Vrc6 || *selected == Inner::Vrc2 || *selected == Inner::Vrc4 ||
        *selected == Inner::Vrc23 || *selected == Inner::Vrc1 || *selected == Inner::H3001) {
        return vrc_mirror(registers_.vrc_mirror);
    }
    return (registers_.flags & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

std::optional<Address> Mapper446::nametable_address(Address addr) const {
    if (inner() != Inner::Tksrom) {
        return std::nullopt;
    }
    return static_cast<Address>(((mmc3_chr_bank(addr & 0x0FFFU) & 0x80U) << 3U) | (addr & 0x03FFU));
}

bool Mapper446::maps_expansion(Address addr) const {
    return (addr >= 0x5000U && addr <= 0x5006U) ||
           (inner() == Inner::Mapper189 && addr >= 0x4020U && addr < 0x8000U);
}

Byte Mapper446::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0U : 0xFFU;
}

Byte Mapper446::read_expansion(Address) {
    return 0U;
}

void Mapper446::write_expansion(Address addr, Byte value) {
    if (addr >= 0x5000U && addr <= 0x5006U) {
        if (registers_.locked) {
            return;
        }
        switch (addr) {
        case 0x5000U:
            registers_.selection = value & 0x1FU;
            registers_.locked = (value & 0x80U) != 0U;
            break;
        case 0x5001U:
            registers_.outer_prg_lsb = value;
            break;
        case 0x5002U:
            registers_.outer_prg_msb = value;
            break;
        case 0x5003U:
            registers_.prg_mask = value;
            break;
        case 0x5004U:
            registers_.flags = value & kSupervisorFlagsMask;
            break;
        case 0x5005U:
            registers_.chr_protect = value & kChrProtectMask;
            break;
        case 0x5006U:
            registers_.outer_chr = value & kOuterChrMask;
            break;
        default:
            std::unreachable();
        }
        return;
    }
    if (inner() == Inner::Mapper189 && addr >= 0x4020U && addr < 0x8000U) {
        write_prg(addr, value);
    }
}

void Mapper446::write_mmc1(Address addr, Byte value) {
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

void Mapper446::write_mmc1_register(Address addr, Byte value) {
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

void Mapper446::write_mmc3(Address addr, Byte value) {
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
    case 0xA001U:
        registers_.mmc3_ram_control = value & 0xC0U;
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
}

Byte Mapper446::vrc_port(Address addr) const {
    const auto value = static_cast<unsigned>(addr);
    const auto normal = static_cast<Byte>(((value >> 1U) & 1U) | ((value & 1U) << 1U));
    if (inner() == Inner::Vrc6 || inner() == Inner::Vrc2) {
        return normal;
    }
    const auto alternate = static_cast<Byte>(((value >> 3U) & 1U) | ((value >> 1U) & 2U));
    return normal | alternate;
}

void Mapper446::write_vrc(Address addr, Byte value) {
    const auto selected = inner().value();
    const auto group = static_cast<unsigned>(addr) >> 12U;
    if (selected == Inner::Vrc1) {
        if (group == 8U || group == 10U || group == 12U) {
            registers_.vrc_prg[(group - 8U) / 2U] = value & 0x0FU;
        } else if (group == 9U) {
            registers_.vrc_mirror = value & 1U;
            registers_.vrc_chr[0U] = (registers_.vrc_chr[0U] & 0x0FU) | ((value & 2U) << 3U);
            registers_.vrc_chr[1U] = (registers_.vrc_chr[1U] & 0x0FU) | ((value & 4U) << 2U);
        } else if (group == 14U || group == 15U) {
            auto& bank = registers_.vrc_chr[group - 14U];
            bank = (bank & 0x10U) | (value & 0x0FU);
        }
        return;
    }
    const auto index = vrc_port(addr);
    if (group == 8U || group == 10U) {
        registers_.vrc_prg[group == 8U ? 0U : 1U] = value & 0x1FU;
    } else if (group == 9U) {
        if (selected == Inner::Vrc2 || index == 0U) {
            registers_.vrc_mirror = value & (selected == Inner::Vrc2 ? 1U : 3U);
        } else if (index == 2U) {
            registers_.vrc_mode = value & 3U;
        }
    } else if (group >= 11U && group <= 14U) {
        auto& bank = registers_.vrc_chr[(group - 11U) * 2U + (index >> 1U)];
        if ((index & 1U) == 0U) {
            bank = (bank & 0x1F0U) | (value & 0x0FU);
        } else {
            bank = (bank & 0x00FU) | static_cast<std::uint16_t>((value & 0x0FU) << 4U);
            if (selected != Inner::Vrc2) {
                bank = (bank & 0x00FU) | static_cast<std::uint16_t>((value & 0x1FU) << 4U);
            }
        }
    } else if (group == 15U && selected != Inner::Vrc2) {
        switch (index) {
        case 0U:
            registers_.vrc_irq_latch = (registers_.vrc_irq_latch & 0xF0U) | (value & 0x0FU);
            break;
        case 1U:
            registers_.vrc_irq_latch =
                (registers_.vrc_irq_latch & 0x0FU) | static_cast<Byte>((value & 0x0FU) << 4U);
            break;
        case 2U:
            vrc_irq_control(value);
            break;
        case 3U:
            vrc_irq_acknowledge();
            break;
        default:
            break;
        }
    }
}

void Mapper446::write_vrc6(Address addr, Byte value) {
    Address decoded = addr;
    decoded =
        static_cast<Address>((decoded & ~3U) | ((decoded & 1U) << 1U) | ((decoded & 2U) >> 1U));
    switch (decoded & 0xF003U) {
    case 0x8000U:
    case 0x8001U:
    case 0x8002U:
    case 0x8003U:
        registers_.vrc_prg[0U] = value & 0x0FU;
        break;
    case 0xC000U:
    case 0xC001U:
    case 0xC002U:
    case 0xC003U:
        registers_.vrc_prg[1U] = value & 0x1FU;
        break;
    case 0x9000U:
        registers_.pulses[0U].control = value;
        break;
    case 0x9001U:
        registers_.pulses[0U].period = (registers_.pulses[0U].period & 0x0F00U) | value;
        break;
    case 0x9002U:
        registers_.pulses[0U].period =
            (registers_.pulses[0U].period & 0x00FFU) | ((value & 0x0FU) << 8U);
        registers_.pulses[0U].enabled = (value & 0x80U) != 0U;
        break;
    case 0x9003U:
        registers_.audio_control = value & 7U;
        break;
    case 0xA000U:
        registers_.pulses[1U].control = value;
        break;
    case 0xA001U:
        registers_.pulses[1U].period = (registers_.pulses[1U].period & 0x0F00U) | value;
        break;
    case 0xA002U:
        registers_.pulses[1U].period =
            (registers_.pulses[1U].period & 0x00FFU) | ((value & 0x0FU) << 8U);
        registers_.pulses[1U].enabled = (value & 0x80U) != 0U;
        break;
    case 0xB000U:
        registers_.saw.rate = value & 0x3FU;
        break;
    case 0xB001U:
        registers_.saw.period = (registers_.saw.period & 0x0F00U) | value;
        break;
    case 0xB002U:
        registers_.saw.period = (registers_.saw.period & 0x00FFU) | ((value & 0x0FU) << 8U);
        registers_.saw.enabled = (value & 0x80U) != 0U;
        break;
    case 0xB003U:
        registers_.vrc_mirror = (value >> 2U) & 3U;
        break;
    case 0xD000U:
    case 0xD001U:
    case 0xD002U:
    case 0xD003U:
        registers_.vrc_chr[decoded & 3U] = value;
        break;
    case 0xE000U:
    case 0xE001U:
    case 0xE002U:
    case 0xE003U:
        registers_.vrc_chr[4U + (decoded & 3U)] = value;
        break;
    case 0xF000U:
        registers_.vrc_irq_latch = value;
        break;
    case 0xF001U:
        vrc_irq_control(value);
        break;
    case 0xF002U:
        vrc_irq_acknowledge();
        break;
    default:
        break;
    }
}

void Mapper446::write_h3001(Address addr, Byte value) {
    switch (addr & 0xF000U) {
    case 0x8000U:
        registers_.h3001_prg[0U] = value & 0x3FU;
        break;
    case 0xA000U:
        registers_.h3001_prg[1U] = value & 0x3FU;
        break;
    case 0xB000U:
        registers_.h3001_chr[addr & 7U] = value;
        break;
    case 0x9000U:
        switch (addr & 7U) {
        case 0U:
            registers_.h3001_swap = (value & 0x80U) != 0U ? 1U : 0U;
            break;
        case 1U:
            registers_.vrc_mirror = (value & 0x40U) != 0U ? 2U : (value & 0x80U) != 0U ? 1U : 0U;
            break;
        case 3U:
            registers_.h3001_irq_enabled = (value & 0x80U) != 0U ? 1U : 0U;
            registers_.h3001_irq_pending = 0U;
            break;
        case 4U:
            registers_.h3001_irq_counter = registers_.h3001_irq_reload;
            registers_.h3001_irq_pending = 0U;
            break;
        case 5U:
            registers_.h3001_irq_reload =
                (registers_.h3001_irq_reload & 0x00FFU) | (static_cast<Word>(value) << 8U);
            break;
        case 6U:
            registers_.h3001_irq_reload = (registers_.h3001_irq_reload & 0xFF00U) | value;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

void Mapper446::power_on() {
    reset();
    chr_ram_.assign(chr_ram_.size(), 0U);
    if (nvram_size_ == 0U) {
        prg_ram_.fill(0U);
    }
}

void Mapper446::reset() {
    registers_ = Registers{};
    registers_.selection = submapper_ == 1U || submapper_ == 2U ? 0U : 1U;
    registers_.flags = initial_mirror_ == MirrorMode::Vertical ? 1U : 0U;
    registers_.mmc3_mirror = initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
    registers_.vrc_mirror = registers_.mmc3_mirror;
}

bool Mapper446::irq_pending() const {
    const auto selected = inner();
    if (!selected) {
        return false;
    }
    if (is_mmc3()) {
        return (registers_.mmc3_irq_flags & 4U) != 0U;
    }
    if (*selected == Inner::Vrc6 || *selected == Inner::Vrc4 || *selected == Inner::Vrc23) {
        return registers_.vrc_irq_pending != 0U;
    }
    return *selected == Inner::H3001 && registers_.h3001_irq_pending != 0U;
}

void Mapper446::acknowledge_irq() {
    if (is_mmc3()) {
        registers_.mmc3_irq_flags &= 3U;
    } else if (inner() == Inner::Vrc6 || inner() == Inner::Vrc4 || inner() == Inner::Vrc23) {
        registers_.vrc_irq_pending = 0U;
    }
}

void Mapper446::clock_irq_counter() {
    if (!is_mmc3()) {
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

void Mapper446::vrc_irq_control(Byte value) {
    registers_.vrc_irq_control = value & 7U;
    registers_.vrc_irq_enabled = (value >> 1U) & 1U;
    registers_.vrc_irq_pending = 0U;
    registers_.vrc_irq_prescaler = 341U;
    if (registers_.vrc_irq_enabled != 0U) {
        registers_.vrc_irq_counter = registers_.vrc_irq_latch;
    }
}

void Mapper446::vrc_irq_acknowledge() {
    registers_.vrc_irq_pending = 0U;
    registers_.vrc_irq_enabled = registers_.vrc_irq_control & 1U;
}

void Mapper446::vrc_irq_tick() {
    if (registers_.vrc_irq_counter == 0xFFU) {
        registers_.vrc_irq_counter = registers_.vrc_irq_latch;
        registers_.vrc_irq_pending = 1U;
    } else {
        ++registers_.vrc_irq_counter;
    }
}

void Mapper446::clock_cpu() {
    const auto selected = inner();
    if (selected == Inner::H3001) {
        if (registers_.h3001_irq_enabled != 0U && registers_.h3001_irq_counter != 0U) {
            --registers_.h3001_irq_counter;
            if (registers_.h3001_irq_counter == 0U) {
                registers_.h3001_irq_pending = 1U;
            }
        }
        return;
    }
    if ((selected != Inner::Vrc4 && selected != Inner::Vrc23) || registers_.vrc_irq_enabled == 0U) {
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

bool Mapper446::has_expansion_audio() const {
    return inner() == Inner::Vrc6;
}

void Mapper446::clock_pulse(Pulse& pulse) {
    const unsigned shift = (registers_.audio_control & 4U) != 0U   ? 8U
                           : (registers_.audio_control & 2U) != 0U ? 4U
                                                                   : 0U;
    if (!pulse.enabled) {
        pulse.step = 0U;
        pulse.timer = pulse.period >> shift;
        return;
    }
    if ((registers_.audio_control & 1U) != 0U) {
        return;
    }
    if (pulse.timer == 0U) {
        pulse.timer = pulse.period >> shift;
        pulse.step = (pulse.step + 1U) & 0x0FU;
    } else {
        --pulse.timer;
    }
}

void Mapper446::clock_saw() {
    const unsigned shift = (registers_.audio_control & 4U) != 0U   ? 8U
                           : (registers_.audio_control & 2U) != 0U ? 4U
                                                                   : 0U;
    if (!registers_.saw.enabled) {
        registers_.saw.step = 0U;
        registers_.saw.accumulator = 0U;
        registers_.saw.timer = registers_.saw.period >> shift;
        return;
    }
    if ((registers_.audio_control & 1U) != 0U) {
        return;
    }
    if (registers_.saw.timer == 0U) {
        registers_.saw.timer = registers_.saw.period >> shift;
        ++registers_.saw.step;
        if (registers_.saw.step >= 14U) {
            registers_.saw.step = 0U;
            registers_.saw.accumulator = 0U;
        } else if ((registers_.saw.step & 1U) == 0U) {
            registers_.saw.accumulator += registers_.saw.rate;
        }
    } else {
        --registers_.saw.timer;
    }
}

void Mapper446::clock_audio() {
    if (inner() != Inner::Vrc6) {
        return;
    }
    clock_pulse(registers_.pulses[0U]);
    clock_pulse(registers_.pulses[1U]);
    clock_saw();
    if (registers_.vrc_irq_enabled != 0U) {
        if ((registers_.vrc_irq_control & 4U) != 0U) {
            vrc_irq_tick();
        } else if (registers_.vrc_irq_prescaler <= 3U) {
            registers_.vrc_irq_prescaler += 338U;
            vrc_irq_tick();
        } else {
            registers_.vrc_irq_prescaler -= 3U;
        }
    }
}

Byte Mapper446::pulse_output(const Pulse& pulse) const {
    if (!pulse.enabled) {
        return 0U;
    }
    const Byte volume = pulse.control & 0x0FU;
    if ((pulse.control & 0x80U) != 0U) {
        return volume;
    }
    return pulse.step <= ((pulse.control >> 4U) & 7U) ? volume : 0U;
}

Byte Mapper446::saw_output() const {
    return registers_.saw.enabled ? registers_.saw.accumulator >> 3U : 0U;
}

float Mapper446::audio_output() const {
    if (inner() != Inner::Vrc6) {
        return 0.0F;
    }
    const float mixed = static_cast<float>(pulse_output(registers_.pulses[0U]) +
                                           pulse_output(registers_.pulses[1U]) + saw_output());
    return mixed * expansion_gains::kVrc6;
}

std::uint32_t Mapper446::geometry_tag() const {
    const unsigned chr_code = chr_ram_.size() == 0x020000UZ   ? 1U
                              : chr_ram_.size() == 0x040000UZ ? 2U
                                                              : 0U;
    return submapper_ | (initial_mirror_ == MirrorMode::Vertical ? 4U : 0U) |
           (prg_ram_size_ != 0U ? 8U : 0U) | (nvram_size_ != 0U ? 0x10U : 0U) | (chr_code << 5U);
}

void Mapper446::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_ram_.size()));
    writer.write(geometry_tag());
    writer.write(registers_.selection);
    writer.write(registers_.outer_prg_lsb);
    writer.write(registers_.outer_prg_msb);
    writer.write(registers_.prg_mask);
    writer.write(registers_.flags);
    writer.write(registers_.chr_protect);
    writer.write(registers_.outer_chr);
    writer.write<Byte>(registers_.locked ? 1U : 0U);
    writer.write(registers_.discrete_bank);
    writer.write(registers_.discrete_mirror);
    writer.write(registers_.mmc1_shift);
    writer.write(registers_.mmc1_count);
    writer.write(registers_.mmc1_control);
    writer.write(registers_.mmc1_chr0);
    writer.write(registers_.mmc1_chr1);
    writer.write(registers_.mmc1_prg);
    writer.write(registers_.mmc3_select);
    writer.write_array(registers_.mmc3_banks);
    writer.write(registers_.mmc3_mirror);
    writer.write(registers_.mmc3_ram_control);
    writer.write(registers_.mmc3_irq_counter);
    writer.write(registers_.mmc3_irq_reload);
    writer.write(registers_.mmc3_irq_flags);
    writer.write(registers_.mmc2_prg);
    writer.write_array(registers_.mmc2_chr);
    writer.write_array(registers_.mmc2_latches);
    writer.write_array(registers_.vrc_prg);
    writer.write_array(registers_.vrc_chr);
    writer.write(registers_.vrc_mirror);
    writer.write(registers_.vrc_mode);
    writer.write(registers_.microwire);
    writer.write(registers_.vrc_irq_latch);
    writer.write(registers_.vrc_irq_counter);
    writer.write(registers_.vrc_irq_prescaler);
    writer.write(registers_.vrc_irq_control);
    writer.write(registers_.vrc_irq_enabled);
    writer.write(registers_.vrc_irq_pending);
    writer.write_array(registers_.h3001_prg);
    writer.write_array(registers_.h3001_chr);
    writer.write(registers_.h3001_swap);
    writer.write(registers_.h3001_irq_reload);
    writer.write(registers_.h3001_irq_counter);
    writer.write(registers_.h3001_irq_enabled);
    writer.write(registers_.h3001_irq_pending);
    for (const auto& pulse : registers_.pulses) {
        writer.write(pulse.control);
        writer.write(pulse.period);
        writer.write(pulse.timer);
        writer.write(pulse.step);
        writer.write<Byte>(pulse.enabled ? 1U : 0U);
    }
    writer.write(registers_.saw.rate);
    writer.write(registers_.saw.period);
    writer.write(registers_.saw.timer);
    writer.write(registers_.saw.accumulator);
    writer.write(registers_.saw.step);
    writer.write<Byte>(registers_.saw.enabled ? 1U : 0U);
    writer.write(registers_.audio_control);
    writer.write_bytes(prg_ram_size_ != 0U ? std::span<const Byte>{prg_ram_}
                                           : std::span<const Byte>{});
    writer.write_bytes(chr_ram_);
}

void Mapper446::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto geometry = reader.read<std::uint32_t>();
    Registers next{};
    reader.read(next.selection);
    reader.read(next.outer_prg_lsb);
    reader.read(next.outer_prg_msb);
    reader.read(next.prg_mask);
    reader.read(next.flags);
    reader.read(next.chr_protect);
    reader.read(next.outer_chr);
    const auto locked = reader.read<Byte>();
    reader.read(next.discrete_bank);
    reader.read(next.discrete_mirror);
    reader.read(next.mmc1_shift);
    reader.read(next.mmc1_count);
    reader.read(next.mmc1_control);
    reader.read(next.mmc1_chr0);
    reader.read(next.mmc1_chr1);
    reader.read(next.mmc1_prg);
    reader.read(next.mmc3_select);
    reader.read_array(next.mmc3_banks);
    reader.read(next.mmc3_mirror);
    reader.read(next.mmc3_ram_control);
    reader.read(next.mmc3_irq_counter);
    reader.read(next.mmc3_irq_reload);
    reader.read(next.mmc3_irq_flags);
    reader.read(next.mmc2_prg);
    reader.read_array(next.mmc2_chr);
    reader.read_array(next.mmc2_latches);
    reader.read_array(next.vrc_prg);
    reader.read_array(next.vrc_chr);
    reader.read(next.vrc_mirror);
    reader.read(next.vrc_mode);
    reader.read(next.microwire);
    reader.read(next.vrc_irq_latch);
    reader.read(next.vrc_irq_counter);
    reader.read(next.vrc_irq_prescaler);
    reader.read(next.vrc_irq_control);
    reader.read(next.vrc_irq_enabled);
    reader.read(next.vrc_irq_pending);
    reader.read_array(next.h3001_prg);
    reader.read_array(next.h3001_chr);
    reader.read(next.h3001_swap);
    reader.read(next.h3001_irq_reload);
    reader.read(next.h3001_irq_counter);
    reader.read(next.h3001_irq_enabled);
    reader.read(next.h3001_irq_pending);
    std::array<Byte, 2> pulse_enabled{};
    for (std::size_t index = 0U; index < next.pulses.size(); ++index) {
        auto& pulse = next.pulses[index];
        reader.read(pulse.control);
        reader.read(pulse.period);
        reader.read(pulse.timer);
        reader.read(pulse.step);
        reader.read(pulse_enabled[index]);
    }
    reader.read(next.saw.rate);
    reader.read(next.saw.period);
    reader.read(next.saw.timer);
    reader.read(next.saw.accumulator);
    reader.read(next.saw.step);
    const auto saw_enabled = reader.read<Byte>();
    reader.read(next.audio_control);
    std::vector<Byte> prg_ram;
    std::vector<Byte> chr_ram;
    reader.read_bytes(prg_ram, kPrgRamSize);
    reader.read_bytes(chr_ram, 0x040000UZ);

    const bool valid_mmc1 = next.mmc1_shift <= 0x1FU && next.mmc1_count <= 4U &&
                            next.mmc1_control <= 0x1FU && next.mmc1_chr0 <= 0x1FU &&
                            next.mmc1_chr1 <= 0x1FU && next.mmc1_prg <= 0x1FU;
    const bool valid_mmc3 = (next.mmc3_select & 0x38U) == 0U && next.mmc3_banks[6U] <= 0x3FU &&
                            next.mmc3_banks[7U] <= 0x3FU && next.mmc3_mirror <= 1U &&
                            (next.mmc3_ram_control & 0x3FU) == 0U && next.mmc3_irq_flags <= 7U &&
                            ((next.mmc3_irq_flags & 4U) == 0U || (next.mmc3_irq_flags & 2U) != 0U);
    const bool valid_mmc2 =
        next.mmc2_prg <= 0x0FU && next.mmc2_latches[0U] <= 1U && next.mmc2_latches[1U] <= 1U &&
        std::ranges::all_of(next.mmc2_chr, [](Byte bank) { return bank <= 0x1FU; });
    const bool valid_vrc =
        std::ranges::all_of(next.vrc_prg, [](Byte bank) { return bank <= 0x1FU; }) &&
        std::ranges::all_of(next.vrc_chr, [](std::uint16_t bank) { return bank <= 0x1FFU; }) &&
        next.vrc_mirror <= 3U && next.vrc_mode <= 3U && next.microwire <= 1U &&
        next.vrc_irq_prescaler >= 1U && next.vrc_irq_prescaler <= 341U &&
        next.vrc_irq_control <= 7U && next.vrc_irq_enabled <= 1U && next.vrc_irq_pending <= 1U &&
        (next.vrc_irq_pending == 0U || next.vrc_irq_enabled != 0U);
    const bool valid_h3001 =
        std::ranges::all_of(next.h3001_prg, [](Byte bank) { return bank <= 0x3FU; }) &&
        next.h3001_swap <= 1U && next.h3001_irq_enabled <= 1U && next.h3001_irq_pending <= 1U &&
        (next.h3001_irq_pending == 0U ||
         (next.h3001_irq_enabled != 0U && next.h3001_irq_counter == 0U));
    const bool valid_audio =
        next.audio_control <= 7U && saw_enabled <= 1U && next.saw.rate <= 0x3FU &&
        next.saw.period <= 0x0FFFU && next.saw.step <= 13U &&
        std::ranges::all_of(
            next.pulses,
            [](const Pulse& pulse) { return pulse.period <= 0x0FFFU && pulse.step <= 15U; }) &&
        std::ranges::all_of(pulse_enabled, [](Byte enabled) { return enabled <= 1U; });
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_ram_.size() || geometry != geometry_tag() || next.selection > 0x1FU ||
        (next.flags & ~kSupervisorFlagsMask) != 0U || (next.chr_protect & ~kChrProtectMask) != 0U ||
        next.outer_chr > kOuterChrMask || locked > 1U || !valid_mmc1 || !valid_mmc3 ||
        !valid_mmc2 || !valid_vrc || !valid_h3001 || !valid_audio ||
        prg_ram.size() != prg_ram_size_ || chr_ram.size() != chr_ram_.size()) {
        reader.fail();
        return;
    }

    next.locked = locked != 0U;
    for (std::size_t index = 0U; index < next.pulses.size(); ++index) {
        next.pulses[index].enabled = pulse_enabled[index] != 0U;
    }
    next.saw.enabled = saw_enabled != 0U;
    registers_ = next;
    if (prg_ram_size_ != 0U) {
        std::ranges::copy(prg_ram, prg_ram_.begin());
    }
    chr_ram_ = std::move(chr_ram);
}

std::span<const Byte> Mapper446::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper446::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), nvram_size_), prg_ram_.begin());
}

} // namespace mapperbus::core
