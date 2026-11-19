#include "core/mappers/mapper444.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4440U;
constexpr Byte kOuterMask = 0x1FU;
constexpr Byte kOuterModeMask = 0x0CU;
constexpr Byte kNrom256Mode = 0x04U;
constexpr Byte kSolderPadMode = 0x08U;
constexpr Byte kNrom128Mode = 0x0CU;

} // namespace

// NESdev mapper 444 oldid 22463 leaves reset and physical pad values unspecified.
// Clearing registers and zero-valued pads are deterministic compatibility policies.
Mapper444::Mapper444(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_size_(header.prg_ram_size + header.prg_nvram_size),
      nvram_size_(header.prg_nvram_size), initial_mirror_(header.mirror_mode),
      submapper_(header.submapper) {
    power_on();
}

Result<void> Mapper444::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 444U) {
        return std::unexpected("Mapper 444 requires mapper ID 444");
    }
    if (!header.is_nes2 || header.submapper > 3U) {
        return std::unexpected("Mapper 444 requires NES 2.0 submapper 0 through 3");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 444 requires 32-512 KiB PRG in 8 KiB units and 8-256 KiB CHR in 1 KiB units");
    }
    if ((header.prg_ram_size != 0U && header.prg_ram_size != kWramSize) ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kWramSize) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
        header.has_battery != (header.prg_nvram_size != 0U) || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 444 supports no CPU RAM or one 8 KiB PRG RAM/NVRAM chip and CHR ROM only");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 444 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 444 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper444::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if (slot == 1U) {
        return registers_.bank_registers[7U] & 0x0FU;
    }
    if ((!registers_.prg_mode && slot == 0U) || (registers_.prg_mode && slot == 2U)) {
        return registers_.bank_registers[6U] & 0x0FU;
    }
    return slot == 3U ? 15U : 14U;
}

Byte Mapper444::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((registers_.outer_bank & kOuterModeMask) == kSolderPadMode) {
            return solder_pads_;
        }
        if (!registers_.prg_ram_enabled || prg_ram_size_ == 0U) {
            return 0U;
        }
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    unsigned bank = inner_prg_bank(addr);
    switch (registers_.outer_bank & kOuterModeMask) {
    case kNrom256Mode:
        bank = (registers_.bank_registers[6U] & 0x0CU) | ((addr >> 13U) & 3U);
        break;
    case kNrom128Mode:
        bank = (registers_.bank_registers[6U] & 0x0EU) | ((addr >> 13U) & 1U);
        break;
    default:
        break;
    }
    const unsigned physical = bank | ((registers_.outer_bank & 3U) << 4U);
    const auto offset = physical * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper444::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (registers_.prg_ram_enabled) {
            registers_.outer_bank = value & kOuterMask;
            if (!registers_.prg_ram_write_protected && prg_ram_size_ != 0U) {
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
            registers_.bank_select = value & 7U;
            registers_.prg_mode = (value & 0x40U) != 0U;
            registers_.chr_inversion = (value & 0x80U) != 0U;
        } else {
            registers_.bank_registers[registers_.bank_select] =
                registers_.bank_select >= 6U ? static_cast<Byte>(value & 0x0FU) : value;
        }
    } else if (addr < 0xC000U) {
        if (even) {
            registers_.mirror_mode =
                (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        } else {
            registers_.prg_ram_enabled = (value & 0x80U) != 0U;
            registers_.prg_ram_write_protected = (value & 0x40U) != 0U;
        }
    } else if (addr < 0xE000U) {
        if (even) {
            registers_.irq_reload = value;
        } else {
            registers_.irq_reload_pending = true;
            registers_.irq_counter = 0U;
        }
    } else if (even) {
        registers_.irq_enabled = false;
        registers_.irq_pending = false;
    } else {
        registers_.irq_enabled = true;
    }
}

bool Mapper444::maps_prg(Address addr) const {
    if (addr >= 0x6000U && addr < 0x8000U &&
        (registers_.outer_bank & kOuterModeMask) == kSolderPadMode) {
        return true;
    }
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && registers_.prg_ram_enabled && prg_ram_size_ != 0U);
}

Byte Mapper444::prg_read_mask(Address addr) const {
    if (addr >= 0x6000U && addr < 0x8000U &&
        (registers_.outer_bank & kOuterModeMask) == kSolderPadMode) {
        return 0x03U;
    }
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper444::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (registers_.chr_inversion ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.bank_registers[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.bank_registers[slot - 2U];
}

Byte Mapper444::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const unsigned inner = inner_chr_bank(addr);
    const unsigned a17 = (submapper_ & 1U) == 0U ? registers_.outer_bank & 1U : (inner >> 7U) & 1U;
    const unsigned a18 = (submapper_ & 2U) == 0U ? (registers_.outer_bank >> 1U) & 1U
                                                 : (registers_.outer_bank >> 4U) & 1U;
    const unsigned bank = (inner & 0x7FU) | (a17 << 7U) | (a18 << 8U);
    const auto offset = bank * kChrBankSize + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper444::write_chr(Address, Byte) {}

MirrorMode Mapper444::mirror_mode() const {
    return registers_.mirror_mode;
}

Result<void> Mapper444::configure_board_jumpers(Byte value) {
    if (value > 3U) {
        return std::unexpected("Mapper 444 solder pads accept values 0 through 3");
    }
    solder_pads_ = value;
    return {};
}

void Mapper444::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        prg_ram_.fill(0U);
    }
}

void Mapper444::reset() {
    registers_ = {};
    registers_.mirror_mode = initial_mirror_;
}

bool Mapper444::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper444::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper444::clock_irq_counter() {
    if (registers_.irq_counter == 0U || registers_.irq_reload_pending) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_reload_pending = false;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && registers_.irq_enabled) {
        registers_.irq_pending = true;
    }
}

std::uint16_t Mapper444::memory_tag() const {
    return static_cast<std::uint16_t>(
        (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) | (prg_ram_size_ != 0U ? 2U : 0U) |
        (nvram_size_ != 0U ? 4U : 0U) | (submapper_ << 3U) | (solder_pads_ << 5U));
}

void Mapper444::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_tag());
    writer.write(registers_.outer_bank);
    writer.write(registers_.bank_select);
    writer.write<Byte>((registers_.prg_mode ? 0x40U : 0U) |
                       (registers_.chr_inversion ? 0x80U : 0U));
    writer.write_array(registers_.bank_registers);
    writer.write<Byte>(static_cast<Byte>(registers_.mirror_mode));
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write<Byte>((registers_.irq_reload_pending ? 1U : 0U) |
                       (registers_.irq_enabled ? 2U : 0U) | (registers_.irq_pending ? 4U : 0U));
    writer.write<Byte>((registers_.prg_ram_enabled ? 0x80U : 0U) |
                       (registers_.prg_ram_write_protected ? 0x40U : 0U));
    if (prg_ram_size_ != 0U) {
        writer.write_array(prg_ram_);
    }
}

void Mapper444::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory = reader.read<std::uint16_t>();
    Registers next{};
    reader.read(next.outer_bank);
    reader.read(next.bank_select);
    const auto mode = reader.read<Byte>();
    reader.read_array(next.bank_registers);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    std::array<Byte, kWramSize> prg_ram{};
    if (prg_ram_size_ != 0U) {
        reader.read_array(prg_ram);
    }

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory != memory_tag() ||
        (next.outer_bank & ~kOuterMask) != 0U || next.bank_select > 7U || (mode & 0x3FU) != 0U ||
        next.bank_registers[6U] > 0x0FU || next.bank_registers[7U] > 0x0FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    next.prg_mode = (mode & 0x40U) != 0U;
    next.chr_inversion = (mode & 0x80U) != 0U;
    next.mirror_mode = static_cast<MirrorMode>(mirror);
    next.irq_reload_pending = (irq & 1U) != 0U;
    next.irq_enabled = (irq & 2U) != 0U;
    next.irq_pending = (irq & 4U) != 0U;
    next.prg_ram_enabled = (ram & 0x80U) != 0U;
    next.prg_ram_write_protected = (ram & 0x40U) != 0U;
    registers_ = next;
    if (prg_ram_size_ != 0U) {
        prg_ram_ = prg_ram;
    }
}

std::span<const Byte> Mapper444::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper444::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), nvram_size_), prg_ram_.begin());
}

} // namespace mapperbus::core
