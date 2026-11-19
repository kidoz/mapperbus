#include "core/mappers/mapper391.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x8000UZ;
constexpr std::size_t kMaxPrgSize = 0x80000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kMinChrSize = 0x2000UZ;
constexpr std::size_t kMaxChrSize = 0x80000UZ;
constexpr std::size_t kChrBankSize = 0x400UZ;
constexpr std::size_t kWramSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3910U;
constexpr Byte kGnromMode = 0x20U;
constexpr Byte kNrom256Mode = 0x04U;
constexpr Byte kOuterPrgA17 = 0x08U;
constexpr Byte kOuterChrA17 = 0x40U;
constexpr Byte kOuterLock = 0x80U;

} // namespace

// Original implementation from NESdev mapper 391 oldid 8012. The article does
// not specify reset values; clearing registers on reset and clearing only volatile
// RAM on cold power are deterministic compatibility policies.
Mapper391::Mapper391(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_size_(header.prg_ram_size + header.prg_nvram_size),
      nvram_size_(header.prg_nvram_size), initial_mirror_(header.mirror_mode),
      mirror_mode_(initial_mirror_) {
    power_on();
}

Result<void> Mapper391::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 391U) {
        return std::unexpected("Mapper 391 requires mapper ID 391");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 391 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 391 requires 32-512 KiB PRG in 8 KiB units and 8-512 KiB CHR in 1 KiB units");
    }
    if ((header.prg_ram_size != 0U && header.prg_ram_size != kWramSize) ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kWramSize) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
        header.has_battery != (header.prg_nvram_size != 0U) || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 391 supports no CPU RAM or one 8 KiB PRG RAM/NVRAM chip and CHR ROM only");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 391 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

unsigned Mapper391::inner_prg_bank(Address addr) const {
    unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if ((outer_bank_ & kGnromMode) != 0U) {
        slot &= 1U;
    }
    if (slot == 1U) {
        return bank_registers_[7U] & 0x1FU;
    }
    if ((!prg_mode_ && slot == 0U) || (prg_mode_ && slot == 2U)) {
        return bank_registers_[6U] & 0x1FU;
    }
    return slot == 3U ? 31U : 30U;
}

Byte Mapper391::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!prg_ram_enabled_ || prg_ram_size_ == 0U) {
            return 0U;
        }
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    unsigned bank = inner_prg_bank(addr);
    if ((outer_bank_ & (kGnromMode | kNrom256Mode)) == (kGnromMode | kNrom256Mode)) {
        bank = (bank & ~2U) | ((addr >> 13U) & 2U);
    }
    const unsigned a17 = (outer_bank_ & kOuterPrgA17) != 0U ? outer_bank_ & 1U : (bank >> 4U) & 1U;
    const unsigned physical = (bank & 0x0FU) | (a17 << 4U) | (((outer_bank_ >> 1U) & 1U) << 5U);
    const auto offset = physical * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper391::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_) {
            if ((outer_bank_ & kOuterLock) == 0U) {
                outer_bank_ = value;
                outer_chr_a18_ = (addr & 0x1000U) != 0U;
            }
            if (prg_ram_size_ != 0U) {
                prg_ram_[addr & 0x1FFFU] = value;
            }
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            bank_select_ = value & 7U;
            prg_mode_ = (value & 0x40U) != 0U;
            chr_inversion_ = (value & 0x80U) != 0U;
        } else {
            bank_registers_[bank_select_] =
                bank_select_ >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        }
    } else if (addr < 0xC000U) {
        if (even) {
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        } else {
            prg_ram_enabled_ = (value & 0x80U) != 0U;
            prg_ram_write_protected_ = (value & 0x40U) != 0U;
        }
    } else if (addr < 0xE000U) {
        if (even) {
            irq_reload_ = value;
        } else {
            irq_reload_pending_ = true;
            irq_counter_ = 0U;
        }
    } else if (even) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

bool Mapper391::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && prg_ram_enabled_ && prg_ram_size_ != 0U);
}

Byte Mapper391::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper391::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    if (slot < 4U) {
        return (bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return bank_registers_[slot - 2U];
}

Byte Mapper391::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const unsigned inner = inner_chr_bank(addr);
    const unsigned a17 =
        (outer_bank_ & kOuterChrA17) != 0U ? (outer_bank_ >> 4U) & 1U : (inner >> 4U) & 1U;
    const unsigned bank =
        (inner & 0x0FU) | (a17 << 4U) | (static_cast<unsigned>(outer_chr_a18_) << 5U);
    const auto offset = bank * kChrBankSize + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper391::write_chr(Address, Byte) {}

MirrorMode Mapper391::mirror_mode() const {
    return mirror_mode_;
}

void Mapper391::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        prg_ram_.fill(0U);
    }
}

void Mapper391::reset() {
    outer_bank_ = 0U;
    outer_chr_a18_ = false;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_pending_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    mirror_mode_ = initial_mirror_;
}

bool Mapper391::irq_pending() const {
    return irq_pending_;
}

void Mapper391::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper391::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_pending_) {
        irq_counter_ = irq_reload_;
        irq_reload_pending_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_) {
        irq_pending_ = true;
    }
}

std::uint16_t Mapper391::memory_tag() const {
    return static_cast<std::uint16_t>((initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (prg_ram_size_ != 0U ? 2U : 0U) |
                                      (nvram_size_ != 0U ? 4U : 0U));
}

void Mapper391::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_tag());
    writer.write(outer_bank_);
    writer.write<Byte>(outer_chr_a18_ ? 1U : 0U);
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_pending_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
    if (prg_ram_size_ != 0U) {
        writer.write_array(prg_ram_);
    }
}

void Mapper391::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory = reader.read<std::uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto outer_chr_a18 = reader.read<Byte>();
    const auto select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    std::array<Byte, kWramSize> prg_ram{};
    if (prg_ram_size_ != 0U) {
        reader.read_array(prg_ram);
    }

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory != memory_tag() || outer_chr_a18 > 1U ||
        select > 7U || (mode & 0x3FU) != 0U || banks[6U] > 0x3FU || banks[7U] > 0x3FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer;
    outer_chr_a18_ = outer_chr_a18 != 0U;
    bank_select_ = select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_pending_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    prg_ram_enabled_ = (ram & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram & 0x40U) != 0U;
    if (prg_ram_size_ != 0U) {
        prg_ram_ = prg_ram;
    }
}

std::span<const Byte> Mapper391::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper391::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), nvram_size_), prg_ram_.begin());
}

} // namespace mapperbus::core
