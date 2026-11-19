#include "core/mappers/mapper393.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x3930U;
constexpr Byte kOuterMask = 0x3FU;
constexpr Byte kChrRamSelect = 0x08U;

} // namespace

// Original implementation from NESdev mapper 393 oldid 8014. Reset behavior is
// unspecified; clearing registers on reset and volatile RAM only on cold power
// are deterministic compatibility policies.
Mapper393::Mapper393(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      wram_size_(header.prg_ram_size + header.prg_nvram_size), persistent_wram_(header.has_battery),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper393::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 393U) {
        return std::unexpected("Mapper 393 requires mapper ID 393");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 393 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 393 requires 32 KiB-1 MiB PRG ROM in 8 KiB units");
    }
    if (chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected("Mapper 393 requires 8-256 KiB CHR ROM in 1 KiB units");
    }
    const bool valid_wram =
        (header.prg_ram_size == 0U || header.prg_ram_size == kWramSize) &&
        (header.prg_nvram_size == 0U || header.prg_nvram_size == kWramSize) &&
        (header.prg_ram_size == 0U || header.prg_nvram_size == 0U) &&
        (header.has_battery ? header.prg_nvram_size == kWramSize : header.prg_nvram_size == 0U);
    if (!valid_wram || header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 393 requires 8 KiB CHR RAM and at most one 8 KiB CPU WRAM chip");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 393 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

std::size_t Mapper393::prg_offset(Address addr) const {
    const auto outer = static_cast<std::size_t>(registers_.outer & 7U) << 17U;
    switch ((registers_.outer >> 4U) & 3U) {
    case 2U:
        return outer | (static_cast<std::size_t>((registers_.banks[6] >> 2U) & 3U) << 15U) |
               (addr & 0x7FFFU);
    case 3U: {
        const unsigned bank = addr < 0xC000U ? registers_.unrom_bank : 7U;
        return outer | (static_cast<std::size_t>(bank) << 14U) | (addr & 0x3FFFU);
    }
    default:
        break;
    }

    const unsigned slot = (addr - 0x8000U) >> 13U;
    const bool swapped = (registers_.bank_mode & 0x40U) != 0U;
    const unsigned inner = slot == 1U                ? registers_.banks[7] & 0x0FU
                           : slot == 3U              ? 15U
                           : (slot == 0U) == swapped ? 14U
                                                     : registers_.banks[6] & 0x0FU;
    return outer | (static_cast<std::size_t>(inner) << 13U) | (addr & 0x1FFFU);
}

Byte Mapper393::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return (registers_.ram_flags & 0x80U) != 0U && wram_size_ != 0U ? wram_[addr & 0x1FFFU]
                                                                        : 0U;
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper393::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((registers_.ram_flags & 0xC0U) == 0x80U) {
            registers_.outer = value & kOuterMask;
            if (wram_size_ != 0U) {
                wram_[addr & 0x1FFFU] = value;
            }
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    registers_.unrom_bank = value & 7U;
    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.bank_select = value & 7U;
        registers_.bank_mode = value & 0xC0U;
        break;
    case 0x8001U:
        registers_.banks[registers_.bank_select] =
            registers_.bank_select >= 6U ? value & 0x3FU : value;
        break;
    case 0xA000U:
        registers_.mirror = (value & 1U) != 0U ? 0U : 1U;
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

bool Mapper393::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U &&
                               (registers_.ram_flags & 0x80U) != 0U && wram_size_ != 0U);
}

Byte Mapper393::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper393::chr_bank(Address addr) const {
    unsigned slot = (static_cast<unsigned>(addr) >> 10U) & 7U;
    if ((registers_.bank_mode & 0x80U) != 0U) {
        slot ^= 4U;
    }
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

Byte Mapper393::read_chr(Address addr) {
    if ((registers_.outer & kChrRamSelect) != 0U) {
        return chr_ram_[addr & 0x1FFFU];
    }
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(chr_bank(addr)) * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper393::write_chr(Address addr, Byte value) {
    if ((registers_.outer & kChrRamSelect) != 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper393::mirror_mode() const {
    return static_cast<MirrorMode>(registers_.mirror);
}

void Mapper393::power_on() {
    reset();
    if (!persistent_wram_) {
        wram_.fill(0U);
    }
    chr_ram_.fill(0U);
}

void Mapper393::reset() {
    registers_ = Registers{};
    registers_.mirror = static_cast<Byte>(initial_mirror_);
}

bool Mapper393::irq_pending() const {
    return (registers_.irq_flags & 4U) != 0U;
}

void Mapper393::acknowledge_irq() {
    registers_.irq_flags &= 3U;
}

void Mapper393::clock_irq_counter() {
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

std::span<const Byte> Mapper393::battery_ram() const {
    return persistent_wram_ ? std::span<const Byte>(wram_).first(wram_size_)
                            : std::span<const Byte>{};
}

void Mapper393::set_battery_ram(std::span<const Byte> data) {
    if (persistent_wram_) {
        std::ranges::copy(data.first(std::min(data.size(), wram_size_)), wram_.begin());
    }
}

std::uint16_t Mapper393::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (wram_size_ != 0U ? 2U : 0U) | (persistent_wram_ ? 4U : 0U));
}

void Mapper393::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.bank_select);
    writer.write(registers_.bank_mode);
    writer.write_array(registers_.banks);
    writer.write(registers_.unrom_bank);
    writer.write(registers_.mirror);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write(registers_.irq_flags);
    writer.write(registers_.ram_flags);
    writer.write_array(chr_ram_);
    if (wram_size_ != 0U) {
        writer.write_array(wram_);
    }
}

void Mapper393::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.bank_select);
    reader.read(next.bank_mode);
    reader.read_array(next.banks);
    reader.read(next.unrom_bank);
    reader.read(next.mirror);
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    reader.read(next.irq_flags);
    reader.read(next.ram_flags);
    std::array<Byte, kChrRamSize> chr_ram{};
    std::array<Byte, kWramSize> wram{};
    reader.read_array(chr_ram);
    if (wram_size_ != 0U) {
        reader.read_array(wram);
    }

    const bool valid_registers = (next.outer & ~kOuterMask) == 0U && next.bank_select <= 7U &&
                                 (next.bank_mode & 0x3FU) == 0U && next.banks[6] <= 0x3FU &&
                                 next.banks[7] <= 0x3FU && next.unrom_bank <= 7U &&
                                 next.mirror <= 1U && next.irq_flags <= 7U &&
                                 ((next.irq_flags & 4U) == 0U || (next.irq_flags & 2U) != 0U) &&
                                 (next.ram_flags & 0x3FU) == 0U;
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || !valid_registers) {
        reader.fail();
        return;
    }

    registers_ = next;
    chr_ram_ = chr_ram;
    if (wram_size_ != 0U) {
        wram_ = wram;
    }
}

} // namespace mapperbus::core
