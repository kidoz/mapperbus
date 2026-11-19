#include "core/mappers/mapper455.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kNromBankSize = 0x004000UZ;
constexpr std::size_t kOuterPrgSize = 0x020000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4550U;
constexpr Address kExtraAddressMask = 0xE000U;
constexpr Address kExtraAddressValue = 0x4000U;
constexpr Address kExtraMask = 0x1F27U;
constexpr Byte kExtraDataMask = 0x1FU;
constexpr Address kOuterA18 = 0x20U;
constexpr Address kOuterA17 = 0x04U;
constexpr Address kMmc3Prg256 = 0x02U;
constexpr Address kMmc3Chr256 = 0x01U;
constexpr Address kNromMode = 0x0100U;
constexpr Address kNrom256 = 0x0200U;
constexpr Address kNromBankMask = 0x1C00U;

} // namespace

// Original implementation from NESdev mapper 455 oldid 21018. Electrical reset is
// unspecified; clearing registers and volatile-only RAM on cold power are policies.
Mapper455::Mapper455(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_size_(header.prg_ram_size + header.prg_nvram_size),
      nvram_size_(header.prg_nvram_size), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper455::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 455U) {
        return std::unexpected("Mapper 455 requires mapper ID 455");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 455 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 455 requires 32-512 KiB PRG in 8 KiB units and 8-512 KiB CHR in 1 KiB units");
    }
    if ((header.prg_ram_size != 0U && header.prg_ram_size != kWramSize) ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kWramSize) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
        header.has_battery != (header.prg_nvram_size != 0U) || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 455 supports no CPU RAM or one 8 KiB PRG RAM/NVRAM chip and CHR ROM only");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 455 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 455 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper455::outer_bank() const {
    return ((registers_.extra & kOuterA18) >> 4U) | ((registers_.extra & kOuterA17) >> 2U);
}

unsigned Mapper455::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned mask = (registers_.extra & kMmc3Prg256) != 0U ? 0x1FU : 0x0FU;
    if (slot == 1U) {
        return registers_.bank_registers[7U] & mask;
    }
    if ((!registers_.prg_mode && slot == 0U) || (registers_.prg_mode && slot == 2U)) {
        return registers_.bank_registers[6U] & mask;
    }
    return slot == 3U ? mask : mask - 1U;
}

std::size_t Mapper455::prg_offset(Address addr) const {
    if ((registers_.extra & kNromMode) != 0U) {
        const bool nrom256 = (registers_.extra & kNrom256) != 0U;
        unsigned bank = (registers_.extra & kNromBankMask) >> 10U;
        if (nrom256) {
            bank = (bank & ~1U) | ((addr >> 14U) & 1U);
        }
        return outer_bank() * kOuterPrgSize + bank * kNromBankSize + (addr & (kNromBankSize - 1U));
    }
    const unsigned bank = inner_prg_bank(addr) | (outer_bank() << 4U);
    return bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper455::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!registers_.prg_ram_enabled || prg_ram_size_ == 0U) {
            return 0U;
        }
        return prg_ram_[addr & (kWramSize - 1U)];
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper455::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (registers_.prg_ram_enabled && !registers_.prg_ram_write_protected &&
            prg_ram_size_ != 0U) {
            prg_ram_[addr & (kWramSize - 1U)] = value;
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
                registers_.bank_select >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
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

bool Mapper455::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && registers_.prg_ram_enabled && prg_ram_size_ != 0U);
}

Byte Mapper455::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper455::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (registers_.chr_inversion ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.bank_registers[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.bank_registers[slot - 2U];
}

std::size_t Mapper455::chr_offset(Address addr) const {
    const unsigned mask = (registers_.extra & kMmc3Chr256) != 0U ? 0xFFU : 0x7FU;
    const unsigned bank = (inner_chr_bank(addr) & mask) | (outer_bank() << 7U);
    return bank * kChrBankSize + (addr & (kChrBankSize - 1U));
}

Byte Mapper455::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper455::write_chr(Address, Byte) {}

MirrorMode Mapper455::mirror_mode() const {
    return registers_.mirror_mode;
}

bool Mapper455::maps_expansion(Address addr) const {
    return addr >= 0x4020U && (addr & kExtraAddressMask) == kExtraAddressValue;
}

Byte Mapper455::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0U : 0xFFU;
}

Byte Mapper455::read_expansion(Address) {
    return 0U;
}

void Mapper455::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr)) {
        return;
    }
    registers_.extra =
        static_cast<Address>((addr & (kOuterA18 | kOuterA17 | kMmc3Prg256 | kMmc3Chr256)) |
                             (static_cast<Address>(value & kExtraDataMask) << 8U));
}

void Mapper455::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        prg_ram_.fill(0U);
    }
}

void Mapper455::reset() {
    registers_ = {};
    registers_.mirror_mode = initial_mirror_;
}

bool Mapper455::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper455::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper455::clock_irq_counter() {
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

std::uint16_t Mapper455::memory_tag() const {
    return static_cast<std::uint16_t>((initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (prg_ram_size_ != 0U ? 2U : 0U) |
                                      (nvram_size_ != 0U ? 4U : 0U));
}

void Mapper455::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_tag());
    writer.write(registers_.extra);
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

void Mapper455::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory = reader.read<std::uint16_t>();
    Registers next{};
    reader.read(next.extra);
    reader.read(next.bank_select);
    const auto bank_mode = reader.read<Byte>();
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
        chr_size != chr_rom_.size() || memory != memory_tag() || (next.extra & ~kExtraMask) != 0U ||
        next.bank_select > 7U || (bank_mode & 0x3FU) != 0U || next.bank_registers[6U] > 0x3FU ||
        next.bank_registers[7U] > 0x3FU || mirror > static_cast<Byte>(MirrorMode::Vertical) ||
        irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    next.prg_mode = (bank_mode & 0x40U) != 0U;
    next.chr_inversion = (bank_mode & 0x80U) != 0U;
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

std::span<const Byte> Mapper455::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper455::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), nvram_size_), prg_ram_.begin());
}

} // namespace mapperbus::core
