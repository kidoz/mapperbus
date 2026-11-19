#include "core/mappers/mapper361.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kOuterBankSize = 0x020000UZ;
constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x200000UZ;
constexpr std::size_t kPrgRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3610U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

} // namespace

// NESdev mapper 361 oldid 7951 defines literal $7000's four outer-bank bits and
// the ordinary $E001 MMC3 register decoder; the outer register mask is unknown.
// Inner PRG/CHR pages therefore use four/seven bits within each 128 KiB bank.
// Reset defaults, repeated zero-latch IRQs, and volatile-RAM clearing on cold
// power are deterministic compatibility policies, not measured board behavior.
Mapper361::Mapper361(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_(header.prg_ram_size + header.prg_nvram_size, 0U), nvram_size_(header.prg_nvram_size),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper361::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 361U) {
        return std::unexpected("Mapper 361 requires mapper ID 361");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 361 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxRomSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxRomSize) {
        return std::unexpected(
            "Mapper 361 requires power-of-two 32 KiB-2 MiB PRG and 8 KiB-2 MiB CHR ROM");
    }
    const bool valid_ram = (header.prg_ram_size == 0U || header.prg_ram_size == kPrgRamSize) &&
                           (header.prg_nvram_size == 0U || header.prg_nvram_size == kPrgRamSize) &&
                           !(header.prg_ram_size != 0U && header.prg_nvram_size != 0U);
    if (!valid_ram || header.has_battery != (header.prg_nvram_size != 0U) ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 361 supports one optional 8 KiB PRG RAM/NVRAM chip and no CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 361 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

unsigned Mapper361::prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool mode = (registers_.bank_select & 0x40U) != 0U;
    switch (slot) {
    case 0U:
        return mode ? 14U : registers_.banks[6U] & 0x0FU;
    case 1U:
        return registers_.banks[7U] & 0x0FU;
    case 2U:
        return mode ? registers_.banks[6U] & 0x0FU : 14U;
    default:
        return 15U;
    }
}

Byte Mapper361::chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot =
        ((pattern_addr >> 10U) & 7U) ^ ((registers_.bank_select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

Byte Mapper361::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!registers_.ram_enabled || prg_ram_.empty()) {
            return 0U;
        }
        return prg_ram_[addr & (kPrgRamSize - 1U)];
    }
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(registers_.outer_bank) * kOuterBankSize +
                        prg_bank(addr) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper361::write_prg(Address addr, Byte value) {
    if (addr == 0x7000U) {
        registers_.outer_bank = value & 0x0FU;
    }
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (registers_.ram_enabled && !registers_.ram_write_protected && !prg_ram_.empty()) {
            prg_ram_[addr & (kPrgRamSize - 1U)] = value;
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.bank_select = value & 0xC7U;
        break;
    case 0x8001U: {
        const unsigned index = registers_.bank_select & 7U;
        registers_.banks[index] = index >= 6U ? value & 0x3FU : value;
        break;
    }
    case 0xA000U:
        registers_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        registers_.ram_enabled = (value & 0x80U) != 0U;
        registers_.ram_write_protected = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        registers_.irq_latch = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_reload = true;
        break;
    case 0xE000U:
        registers_.irq_enabled = false;
        registers_.irq_pending = false;
        break;
    case 0xE001U:
        registers_.irq_enabled = true;
        break;
    default:
        break;
    }
}

bool Mapper361::maps_prg(Address addr) const {
    if (addr >= 0x8000U) {
        return true;
    }
    return addr >= 0x6000U && addr < 0x8000U && registers_.ram_enabled && !prg_ram_.empty();
}

Byte Mapper361::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper361::read_chr(Address addr) {
    const auto offset = static_cast<std::size_t>(registers_.outer_bank) * kOuterBankSize +
                        static_cast<std::size_t>(chr_bank(addr) & 0x7FU) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper361::write_chr(Address, Byte) {}

MirrorMode Mapper361::mirror_mode() const {
    return registers_.mirror;
}

void Mapper361::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        std::ranges::fill(prg_ram_, 0U);
    }
}

void Mapper361::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

bool Mapper361::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper361::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper361::clock_irq_counter() {
    if (registers_.irq_counter == 0U || registers_.irq_reload) {
        registers_.irq_counter = registers_.irq_latch;
        registers_.irq_reload = false;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && registers_.irq_enabled) {
        registers_.irq_pending = true;
    }
}

std::uint16_t Mapper361::memory_state_tag() const {
    return static_cast<std::uint16_t>((prg_ram_.empty() ? 0U : 1U) | (nvram_size_ != 0U ? 2U : 0U) |
                                      (initial_mirror_ == MirrorMode::Vertical ? 4U : 0U));
}

void Mapper361::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_state_tag());
    writer.write(registers_.outer_bank);
    writer.write(registers_.bank_select);
    writer.write_array(registers_.banks);
    writer.write<Byte>(registers_.mirror == MirrorMode::Vertical ? 1U : 0U);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_latch);
    writer.write<Byte>((registers_.irq_reload ? kIrqReload : 0U) |
                       (registers_.irq_enabled ? kIrqEnabled : 0U) |
                       (registers_.irq_pending ? kIrqPending : 0U));
    writer.write<Byte>((registers_.ram_enabled ? 0x80U : 0U) |
                       (registers_.ram_write_protected ? 0x40U : 0U));
    writer.write_bytes(prg_ram_);
}

void Mapper361::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory_tag = reader.read<std::uint16_t>();
    Registers next;
    reader.read(next.outer_bank);
    reader.read(next.bank_select);
    reader.read_array(next.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_latch);
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();
    std::vector<Byte> prg_ram;
    reader.read_bytes(prg_ram, kPrgRamSize);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory_tag != memory_state_tag() ||
        next.outer_bank > 0x0FU || (next.bank_select & 0x38U) != 0U || next.banks[6U] > 0x3FU ||
        next.banks[7U] > 0x3FU || mirror > 1U || irq_flags > 7U ||
        ((irq_flags & kIrqPending) != 0U && (irq_flags & kIrqEnabled) == 0U) ||
        (ram_flags & 0x3FU) != 0U || prg_ram.size() != prg_ram_.size()) {
        reader.fail();
        return;
    }

    next.mirror = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    next.irq_reload = (irq_flags & kIrqReload) != 0U;
    next.irq_enabled = (irq_flags & kIrqEnabled) != 0U;
    next.irq_pending = (irq_flags & kIrqPending) != 0U;
    next.ram_enabled = (ram_flags & 0x80U) != 0U;
    next.ram_write_protected = (ram_flags & 0x40U) != 0U;
    registers_ = next;
    prg_ram_ = std::move(prg_ram);
}

std::span<const Byte> Mapper361::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper361::set_battery_ram(std::span<const Byte> data) {
    if (data.size() != nvram_size_) {
        return;
    }
    std::ranges::copy(data, prg_ram_.begin());
}

} // namespace mapperbus::core
