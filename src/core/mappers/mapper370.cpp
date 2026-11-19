#include "core/mappers/mapper370.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kPrgRamSize = 0x2000UZ;
constexpr Address kOuterMask = 0xF000U;
constexpr Address kOuterValue = 0x5000U;
constexpr std::uint16_t kStateTag = 0x3700U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

} // namespace

// NESdev mapper 370 oldid 7976 defines the outer bit diagram, MMC3 clone,
// solder-pad input and TLSROM selection. The diagram's PP and CC positions are
// two outer pins per ROM; the inner masks are applied before combining pins.
// Reset defaults and volatile-RAM clearing on cold power are deterministic
// policies, not measurements of the board's electrical startup behavior.
Mapper370::Mapper370(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_(header.prg_ram_size + header.prg_nvram_size, 0U), nvram_size_(header.prg_nvram_size),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper370::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 370U) {
        return std::unexpected("Mapper 370 requires mapper ID 370");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 370 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxRomSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxRomSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected("Mapper 370 requires 32-512 KiB PRG in 8 KiB units and 8-512 KiB "
                               "CHR ROM in 1 KiB units");
    }
    const bool no_ram = header.prg_ram_size == 0U && header.prg_nvram_size == 0U;
    const bool volatile_ram = header.prg_ram_size == kPrgRamSize && header.prg_nvram_size == 0U;
    const bool battery_ram = header.prg_ram_size == 0U && header.prg_nvram_size == kPrgRamSize;
    if ((!no_ram && !volatile_ram && !battery_ram) || header.has_battery != battery_ram ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 370 supports no PRG RAM, 8 KiB volatile PRG RAM, or 8 KiB battery PRG NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 370 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

bool Mapper370::uses_tlsrom() const {
    return (registers_.outer & 7U) == 1U;
}

unsigned Mapper370::prg_bank(Address addr) const {
    const unsigned count = (registers_.outer & 0x20U) != 0U ? 32U : 16U;
    const unsigned mask = count - 1U;
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (registers_.bank_select & 0x40U) != 0U;
    switch (slot) {
    case 0U:
        return swapped ? count - 2U : registers_.banks[6U] & mask;
    case 1U:
        return registers_.banks[7U] & mask;
    case 2U:
        return swapped ? registers_.banks[6U] & mask : count - 2U;
    default:
        return count - 1U;
    }
}

unsigned Mapper370::chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot =
        ((pattern_addr >> 10U) & 7U) ^ ((registers_.bank_select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.banks[slot - 2U];
}

unsigned Mapper370::prg_rom_bank(unsigned bank) const {
    const unsigned inner_mask = (registers_.outer & 0x20U) != 0U ? 0x1FU : 0x0FU;
    const unsigned outer = (registers_.outer >> 3U) & 3U;
    return (outer << 4U) | (bank & inner_mask);
}

unsigned Mapper370::chr_rom_bank(unsigned bank) const {
    const unsigned inner_mask = (registers_.outer & 4U) != 0U ? 0xFFU : 0x7FU;
    const unsigned outer = registers_.outer & 3U;
    return (outer << 7U) | (bank & inner_mask);
}

Byte Mapper370::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return maps_prg(addr) ? prg_ram_[addr & 0x1FFFU] : 0U;
    }
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(prg_rom_bank(prg_bank(addr))) * kPrgBankSize +
                        (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper370::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (maps_prg(addr) && !registers_.ram_write_protected) {
            prg_ram_[addr & 0x1FFFU] = value;
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
        if (!uses_tlsrom()) {
            registers_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        }
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

bool Mapper370::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && !prg_ram_.empty() && registers_.ram_enabled);
}

Byte Mapper370::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper370::read_chr(Address addr) {
    const auto offset = static_cast<std::size_t>(chr_rom_bank(chr_bank(addr))) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper370::write_chr(Address, Byte) {}

MirrorMode Mapper370::mirror_mode() const {
    if (!uses_tlsrom()) {
        return registers_.mirror;
    }
    const auto a = chr_bank(0x0000U) & 0x80U;
    const auto b = chr_bank(0x0400U) & 0x80U;
    const auto c = chr_bank(0x0800U) & 0x80U;
    const auto d = chr_bank(0x0C00U) & 0x80U;
    if (a == b && b == c && c == d) {
        return a != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
    if (a == b && c == d) {
        return MirrorMode::Horizontal;
    }
    return MirrorMode::Vertical;
}

std::optional<Address> Mapper370::nametable_address(Address addr) const {
    if (!uses_tlsrom()) {
        return std::nullopt;
    }
    return static_cast<Address>(((chr_bank(addr & 0x0FFFU) & 0x80U) << 3U) | (addr & 0x03FFU));
}

bool Mapper370::maps_expansion(Address addr) const {
    return (addr & kOuterMask) == kOuterValue;
}

Byte Mapper370::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x80U : 0U;
}

Byte Mapper370::read_expansion(Address addr) {
    return maps_expansion(addr) ? static_cast<Byte>(solder_pad_ << 7U) : 0U;
}

void Mapper370::write_expansion(Address addr, Byte value) {
    if (maps_expansion(addr)) {
        registers_.outer = value & 0x3FU;
    }
}

Result<void> Mapper370::configure_board_jumpers(Byte value) {
    if (value > 1U) {
        return std::unexpected("Mapper 370 solder pad accepts values 0 and 1");
    }
    solder_pad_ = value;
    return {};
}

void Mapper370::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        std::fill(prg_ram_.begin(), prg_ram_.end(), 0U);
    }
}

void Mapper370::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

bool Mapper370::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper370::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper370::clock_irq_counter() {
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

std::span<const Byte> Mapper370::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper370::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == nvram_size_) {
        std::copy(data.begin(), data.end(), prg_ram_.begin());
    }
}

std::uint16_t Mapper370::memory_state_tag() const {
    const unsigned population = nvram_size_ != 0U ? 2U : !prg_ram_.empty() ? 1U : 0U;
    return static_cast<std::uint16_t>((initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (population << 1U) |
                                      (static_cast<unsigned>(solder_pad_) << 4U));
}

void Mapper370::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_state_tag());
    writer.write(registers_.outer);
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

void Mapper370::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory_tag = reader.read<std::uint16_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.bank_select);
    reader.read_array(next.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_latch);
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();
    std::vector<Byte> next_ram;
    reader.read_bytes(next_ram, kPrgRamSize);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory_tag != memory_state_tag() || next.outer > 0x3FU ||
        (next.bank_select & 0x38U) != 0U || next.banks[6U] > 0x3FU || next.banks[7U] > 0x3FU ||
        mirror > 1U || irq_flags > 7U ||
        ((irq_flags & kIrqPending) != 0U && (irq_flags & kIrqEnabled) == 0U) ||
        (ram_flags & 0x3FU) != 0U || next_ram.size() != prg_ram_.size()) {
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
    prg_ram_ = std::move(next_ram);
}

} // namespace mapperbus::core
