#include "core/mappers/mapper367.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x080000UZ;
constexpr std::uint16_t kStateTag = 0x3670U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

} // namespace

// NESdev mapper 367 oldid 7968 selects the Mapper 205 outer block from CPU A1:A0
// instead of write data. Apply outer pins after MMC3 bank selection and before
// fitted-ROM wrapping. Reset defaults and repeated zero-latch IRQs are shared
// compatibility policies, not measurements of this board's reset/IRQ circuitry.
Mapper367::Mapper367(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper367::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 367U) {
        return std::unexpected("Mapper 367 requires mapper ID 367");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 367 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxRomSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxRomSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected("Mapper 367 requires 32-512 KiB PRG in 8 KiB units and 8-512 KiB "
                               "CHR ROM in 1 KiB units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 367 requires ROM-only media without RAM, NVRAM, or battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 367 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

unsigned Mapper367::prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool mode = (registers_.bank_select & 0x40U) != 0U;
    switch (slot) {
    case 0U:
        return mode ? 62U : registers_.banks[6U] & 0x3FU;
    case 1U:
        return registers_.banks[7U] & 0x3FU;
    case 2U:
        return mode ? registers_.banks[6U] & 0x3FU : 62U;
    default:
        return 63U;
    }
}

unsigned Mapper367::chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot =
        ((pattern_addr >> 10U) & 7U) ^ ((registers_.bank_select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.banks[slot - 2U];
}

unsigned Mapper367::prg_rom_bank(unsigned bank) const {
    const unsigned mask = registers_.block < 2U ? 0x1FU : 0x0FU;
    return (bank & mask) | (static_cast<unsigned>(registers_.block) << 4U);
}

unsigned Mapper367::chr_rom_bank(unsigned bank) const {
    const unsigned mask = registers_.block < 2U ? 0xFFU : 0x7FU;
    return (bank & mask) | (static_cast<unsigned>(registers_.block) << 7U);
}

Byte Mapper367::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(prg_rom_bank(prg_bank(addr))) * kPrgBankSize +
                        (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper367::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000U) == 0x6000U) {
        registers_.block = addr & 3U;
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

bool Mapper367::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper367::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : 0U;
}

Byte Mapper367::read_chr(Address addr) {
    const auto offset = static_cast<std::size_t>(chr_rom_bank(chr_bank(addr))) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper367::write_chr(Address, Byte) {}

MirrorMode Mapper367::mirror_mode() const {
    return registers_.mirror;
}

void Mapper367::power_on() {
    reset();
}

void Mapper367::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

bool Mapper367::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper367::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper367::clock_irq_counter() {
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

std::uint16_t Mapper367::memory_state_tag() const {
    return initial_mirror_ == MirrorMode::Vertical ? 1U : 0U;
}

void Mapper367::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_state_tag());
    writer.write(registers_.block);
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
}

void Mapper367::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory_tag = reader.read<std::uint16_t>();
    Registers next;
    reader.read(next.block);
    reader.read(next.bank_select);
    reader.read_array(next.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_latch);
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory_tag != memory_state_tag() || next.block > 3U ||
        (next.bank_select & 0x38U) != 0U || next.banks[6U] > 0x3FU || next.banks[7U] > 0x3FU ||
        mirror > 1U || irq_flags > 7U ||
        ((irq_flags & kIrqPending) != 0U && (irq_flags & kIrqEnabled) == 0U) ||
        (ram_flags & 0x3FU) != 0U) {
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
}

} // namespace mapperbus::core
