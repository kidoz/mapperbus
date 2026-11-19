#include "core/mappers/mapper430.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x4300U;
constexpr Byte kGnromMode = 0x08U;
constexpr Byte kChrA17Override = 0x04U;

} // namespace

// Original implementation from NES 2.0 mapper 430 oldid 18979. Reset clears the
// outer and MMC3 registers and retains WRAM; cold power clears only volatile WRAM.
Mapper430::Mapper430(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      wram_size_(header.prg_ram_size + header.prg_nvram_size), nvram_size_(header.prg_nvram_size),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper430::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 430U) {
        return std::unexpected("Mapper 430 requires mapper ID 430");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 430 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 430 requires 32-512 KiB PRG in 8 KiB units and 8-512 KiB CHR in 1 KiB units");
    }
    const bool valid_wram = (header.prg_ram_size == 0U || header.prg_ram_size == kWramSize) &&
                            (header.prg_nvram_size == 0U || header.prg_nvram_size == kWramSize) &&
                            (header.prg_ram_size == 0U || header.prg_nvram_size == 0U) &&
                            header.has_battery == (header.prg_nvram_size == kWramSize);
    if (!valid_wram || header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 430 supports no CPU RAM or one 8 KiB PRG RAM/NVRAM chip and CHR ROM only");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 430 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 430 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper430::mmc3_prg_bank(Address addr) const {
    unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if ((registers_.outer & kGnromMode) != 0U) {
        slot &= 1U;
    }
    const bool swapped = (registers_.bank_mode & 0x40U) != 0U;
    return slot == 1U                ? registers_.banks[7U] & 0x3FU
           : slot == 3U              ? 15U
           : (slot == 0U) == swapped ? 14U
                                     : registers_.banks[6U] & 0x3FU;
}

Byte Mapper430::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return (registers_.ram_flags & 0x80U) != 0U && wram_size_ != 0U ? wram_[addr & 0x1FFFU]
                                                                        : 0U;
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const unsigned bank =
        (mmc3_prg_bank(addr) & 0x0FU) | (static_cast<unsigned>(registers_.outer & 3U) << 4U);
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper430::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (registers_.ram_flags == 0x80U) {
            registers_.outer = value & 0x0FU;
            if (wram_size_ != 0U) {
                wram_[addr & 0x1FFFU] = value;
            }
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.bank_select = value & 7U;
        registers_.bank_mode = value & 0xC0U;
        break;
    case 0x8001U:
        registers_.banks[registers_.bank_select] =
            registers_.bank_select >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        break;
    case 0xA000U:
        registers_.mirror = value & 1U;
        break;
    case 0xA001U:
        registers_.ram_flags = value & 0xC0U;
        break;
    case 0xC000U:
        registers_.irq_reload = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_flags |= 1U;
        break;
    case 0xE000U:
        registers_.irq_flags &= 1U;
        break;
    case 0xE001U:
        registers_.irq_flags |= 2U;
        break;
    default:
        break;
    }
}

bool Mapper430::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U &&
                               (registers_.ram_flags & 0x80U) != 0U && wram_size_ != 0U);
}

Byte Mapper430::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper430::mmc3_chr_bank(Address addr) const {
    const unsigned slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^
                          ((registers_.bank_mode & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

Byte Mapper430::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const unsigned inner = mmc3_chr_bank(addr);
    const unsigned a17 =
        (registers_.outer & kChrA17Override) != 0U ? registers_.outer & 1U : (inner >> 7U) & 1U;
    const unsigned bank = (inner & 0x7FU) | (a17 << 7U) |
                          (static_cast<unsigned>((registers_.outer >> 1U) & 1U) << 8U);
    const auto offset = static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper430::write_chr(Address, Byte) {}

MirrorMode Mapper430::mirror_mode() const {
    return (registers_.mirror & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper430::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        wram_.fill(0U);
    }
}

void Mapper430::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_ == MirrorMode::Vertical ? 0U : 1U;
}

bool Mapper430::irq_pending() const {
    return (registers_.irq_flags & 4U) != 0U;
}

void Mapper430::acknowledge_irq() {
    registers_.irq_flags &= 3U;
}

void Mapper430::clock_irq_counter() {
    if (registers_.irq_counter == 0U || (registers_.irq_flags & 1U) != 0U) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_flags &= 6U;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & 2U) != 0U) {
        registers_.irq_flags |= 4U;
    }
}

std::span<const Byte> Mapper430::battery_ram() const {
    return std::span<const Byte>(wram_).first(nvram_size_);
}

void Mapper430::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == nvram_size_) {
        std::ranges::copy(data, wram_.begin());
    }
}

std::uint16_t Mapper430::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (wram_size_ != 0U ? 2U : 0U) | (nvram_size_ != 0U ? 4U : 0U));
}

void Mapper430::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.bank_select);
    writer.write(registers_.bank_mode);
    writer.write_array(registers_.banks);
    writer.write(registers_.mirror);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write(registers_.irq_flags);
    writer.write(registers_.ram_flags);
    if (wram_size_ != 0U) {
        writer.write_array(wram_);
    }
}

void Mapper430::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.bank_select);
    reader.read(next.bank_mode);
    reader.read_array(next.banks);
    reader.read(next.mirror);
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    reader.read(next.irq_flags);
    reader.read(next.ram_flags);
    std::array<Byte, kWramSize> wram{};
    if (wram_size_ != 0U) {
        reader.read_array(wram);
    }

    const bool valid_registers =
        next.outer <= 0x0FU && next.bank_select <= 7U && (next.bank_mode & 0x3FU) == 0U &&
        next.banks[6U] <= 0x3FU && next.banks[7U] <= 0x3FU && next.mirror <= 1U &&
        next.irq_flags <= 7U && ((next.irq_flags & 4U) == 0U || (next.irq_flags & 2U) != 0U) &&
        (next.ram_flags & 0x3FU) == 0U;
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || !valid_registers) {
        reader.fail();
        return;
    }

    registers_ = next;
    if (wram_size_ != 0U) {
        wram_ = wram;
    }
}

} // namespace mapperbus::core
