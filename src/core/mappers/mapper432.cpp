#include "core/mappers/mapper432.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x100000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateBase = 0x4320U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

Byte outer_mask(Byte submapper) {
    return submapper == 4U ? 0x5FU : 0xFFU;
}

} // namespace

// Original implementation from NES 2.0 mapper 432 oldid 23472. Reset clears the
// outer and MMC3 registers and retains WRAM; cold power clears only volatile WRAM.
Mapper432::Mapper432(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      wram_size_(header.prg_ram_size + header.prg_nvram_size), nvram_size_(header.prg_nvram_size),
      submapper_(header.submapper), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper432::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 432U) {
        return std::unexpected("Mapper 432 requires mapper ID 432");
    }
    if (!header.is_nes2 || header.submapper > 4U) {
        return std::unexpected("Mapper 432 requires NES 2.0 submapper 0 through 4");
    }
    const std::size_t max_prg = header.submapper == 0U ? kMaxPrgSize : kMaxPrgSize / 2U;
    const std::size_t max_chr = header.submapper == 0U ? kMaxChrSize : kMaxChrSize / 2U;
    if (prg_size < kMinPrgSize || prg_size > max_prg || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 432 PRG ROM exceeds its decoded 8 KiB bank pins");
    }
    if (chr_size < kMinChrSize || chr_size > max_chr || chr_size % kChrBankSize != 0U) {
        return std::unexpected("Mapper 432 CHR ROM exceeds its decoded 1 KiB bank pins");
    }
    const bool valid_wram = (header.prg_ram_size == 0U || header.prg_ram_size == kWramSize) &&
                            (header.prg_nvram_size == 0U || header.prg_nvram_size == kWramSize) &&
                            (header.prg_ram_size == 0U || header.prg_nvram_size == 0U) &&
                            header.has_battery == (header.prg_nvram_size == kWramSize);
    if (!valid_wram || header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 432 supports no CPU RAM or one 8 KiB PRG RAM/NVRAM chip and CHR ROM only");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 432 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 432 does not provide trainer RAM");
    }
    return {};
}

bool Mapper432::nrom256_mode() const {
    return (submapper_ <= 1U && (registers_.outer & 0x80U) != 0U) ||
           (submapper_ == 2U && (registers_.outer & 0x20U) != 0U);
}

bool Mapper432::solder_read_enabled() const {
    return submapper_ == 1U ? (registers_.outer & 0x20U) != 0U
                            : (registers_.solder_enable & 1U) != 0U;
}

bool Mapper432::prg_rom_disabled() const {
    return submapper_ == 4U && solder_read_enabled() && solder_pad_ != 0U;
}

Byte Mapper432::mmc3_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if ((registers_.outer & 0x40U) != 0U) {
        return registers_.banks[(slot & 1U) == 0U ? 6U : 7U] & 0x3FU;
    }
    if (nrom256_mode()) {
        return static_cast<Byte>((registers_.banks[6U] & 0x3CU) | slot);
    }
    const bool swapped = (registers_.bank_mode & 0x40U) != 0U;
    return slot == 1U                ? registers_.banks[7U] & 0x3FU
           : slot == 3U              ? 63U
           : (slot == 0U) == swapped ? 62U
                                     : registers_.banks[6U] & 0x3FU;
}

Byte Mapper432::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return (registers_.ram_flags & 0x80U) != 0U && wram_size_ != 0U ? wram_[addr & 0x1FFFU]
                                                                        : 0U;
    }
    if (addr < 0x8000U) {
        return 0U;
    }
    if (prg_rom_disabled()) {
        return 0U;
    }
    if (submapper_ != 4U && solder_read_enabled()) {
        return solder_pad_;
    }
    if (prg_rom_.empty()) {
        return 0U;
    }
    const unsigned inner = mmc3_prg_bank(addr);
    const unsigned a17 = (registers_.outer & 2U) != 0U ? registers_.outer & 1U : (inner >> 5U) & 1U;
    unsigned bank = (inner & 0x1FU) | (a17 << 5U) |
                    (static_cast<unsigned>((registers_.outer >> 4U) & 1U) << 6U);
    if (submapper_ == 0U) {
        bank |= static_cast<unsigned>((registers_.outer >> 5U) & 1U) << 7U;
    }
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper432::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((addr & 1U) == 0U) {
            if (submapper_ != 1U) {
                registers_.solder_enable = value & 1U;
            }
        } else if (registers_.ram_flags == 0x80U &&
                   !((submapper_ == 2U || submapper_ == 3U) && (registers_.outer & 0x80U) != 0U)) {
            registers_.outer = value & outer_mask(submapper_);
        }
        if (registers_.ram_flags == 0x80U && wram_size_ != 0U) {
            wram_[addr & 0x1FFFU] = value;
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
        registers_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        registers_.ram_flags = value & 0xC0U;
        break;
    case 0xC000U:
        registers_.irq_reload = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_flags |= kIrqReload;
        break;
    case 0xE000U:
        registers_.irq_flags &= kIrqReload;
        break;
    case 0xE001U:
        registers_.irq_flags |= kIrqEnabled;
        break;
    default:
        break;
    }
}

bool Mapper432::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U &&
                               (registers_.ram_flags & 0x80U) != 0U && wram_size_ != 0U);
}

Byte Mapper432::prg_read_mask(Address addr) const {
    if (!maps_prg(addr) || prg_rom_disabled()) {
        return 0U;
    }
    return 0xFFU;
}

Byte Mapper432::mmc3_chr_bank(Address addr) const {
    const unsigned slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^
                          ((registers_.bank_mode & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

std::uint32_t Mapper432::chr_offset(Address addr) const {
    std::uint32_t offset = 0U;
    if (submapper_ == 3U && (registers_.outer & 0x20U) != 0U) {
        const auto pins =
            static_cast<Address>((addr & 0x1000U) | ((addr & 0x800U) >> 1U) | (addr & 0x800U));
        offset = static_cast<std::uint32_t>(mmc3_chr_bank(pins)) * 0x800U + (addr & 0x7FFU);
    } else {
        offset = static_cast<std::uint32_t>(mmc3_chr_bank(addr)) * kChrBankSize + (addr & 0x3FFU);
    }
    const unsigned a17 =
        (registers_.outer & 4U) != 0U ? registers_.outer & 1U : (offset >> 17U) & 1U;
    offset = (offset & 0x1FFFFU) | (a17 << 17U) |
             (static_cast<std::uint32_t>((registers_.outer >> 3U) & 1U) << 18U);
    if (submapper_ == 0U) {
        offset |= static_cast<std::uint32_t>((registers_.outer >> 5U) & 1U) << 19U;
    }
    return offset;
}

Byte Mapper432::read_chr(Address addr) {
    return chr_rom_.empty() ? 0U : chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper432::write_chr(Address, Byte) {}

MirrorMode Mapper432::mirror_mode() const {
    return registers_.mirror;
}

Result<void> Mapper432::configure_board_jumpers(Byte value) {
    solder_pad_ = value;
    return {};
}

void Mapper432::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        wram_.fill(0U);
    }
}

void Mapper432::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

bool Mapper432::irq_pending() const {
    return (registers_.irq_flags & kIrqPending) != 0U;
}

void Mapper432::acknowledge_irq() {
    registers_.irq_flags &= ~kIrqPending;
}

void Mapper432::clock_irq_counter() {
    if (registers_.irq_counter == 0U || (registers_.irq_flags & kIrqReload) != 0U) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_flags &= ~kIrqReload;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & kIrqEnabled) != 0U) {
        registers_.irq_flags |= kIrqPending;
    }
}

std::span<const Byte> Mapper432::battery_ram() const {
    return std::span<const Byte>(wram_).first(nvram_size_);
}

void Mapper432::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == nvram_size_) {
        std::ranges::copy(data, wram_.begin());
    }
}

std::uint16_t Mapper432::state_tag() const {
    return static_cast<std::uint16_t>(
        kStateBase | submapper_ | (initial_mirror_ == MirrorMode::Vertical ? 0x8000U : 0U) |
        (wram_size_ != 0U ? 0x1000U : 0U) | (nvram_size_ != 0U ? 0x2000U : 0U));
}

void Mapper432::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.solder_enable);
    writer.write(registers_.bank_select);
    writer.write(registers_.bank_mode);
    writer.write_array(registers_.banks);
    writer.write(static_cast<Byte>(registers_.mirror));
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write(registers_.irq_flags);
    writer.write(registers_.ram_flags);
    writer.write(solder_pad_);
    if (wram_size_ != 0U) {
        writer.write_array(wram_);
    }
}

void Mapper432::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.solder_enable);
    reader.read(next.bank_select);
    reader.read(next.bank_mode);
    reader.read_array(next.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    reader.read(next.irq_flags);
    reader.read(next.ram_flags);
    const auto solder_pad = reader.read<Byte>();
    std::array<Byte, kWramSize> wram{};
    if (wram_size_ != 0U) {
        reader.read_array(wram);
    }

    const bool valid_registers =
        (next.outer & ~outer_mask(submapper_)) == 0U && next.solder_enable <= 1U &&
        (submapper_ != 1U || next.solder_enable == 0U) && next.bank_select <= 7U &&
        (next.bank_mode & 0x3FU) == 0U && next.banks[6U] <= 0x3FU && next.banks[7U] <= 0x3FU &&
        mirror <= static_cast<Byte>(MirrorMode::Vertical) && next.irq_flags <= 7U &&
        ((next.irq_flags & kIrqPending) == 0U || (next.irq_flags & kIrqEnabled) != 0U) &&
        (next.ram_flags & 0x3FU) == 0U;
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || solder_pad != solder_pad_ || !valid_registers) {
        reader.fail();
        return;
    }

    next.mirror = static_cast<MirrorMode>(mirror);
    registers_ = next;
    if (wram_size_ != 0U) {
        wram_ = wram;
    }
}

} // namespace mapperbus::core
