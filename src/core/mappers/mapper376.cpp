#include "core/mappers/mapper376.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x080000UZ;
constexpr std::uint16_t kStateTag = 0x7600U;
constexpr Byte kOuterModeMask = 0x60U;
constexpr Byte kNrom128Mode = 0x40U;
constexpr Byte kNrom256Mode = 0x60U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

} // namespace

// Original implementation from NESdev mapper 376 oldid 7986. Outer A17/A18
// follows MMC3 or NROM inner decoding; fitted ROMs mirror only afterwards.
// Register/IRQ reset defaults and repeated zero-latch IRQs are compatibility
// policies, not measured YY841155C reset or clone-counter behavior.
Mapper376::Mapper376(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper376::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 376U) {
        return std::unexpected("Mapper 376 requires mapper ID 376");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 376 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxRomSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxRomSize) {
        return std::unexpected(
            "Mapper 376 requires power-of-two 32 KiB-512 KiB PRG and 8 KiB-512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 376 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 376 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

std::size_t Mapper376::outer_offset() const {
    return (static_cast<std::size_t>(registers_.outer1 & 1U) << 18U) |
           (static_cast<std::size_t>((registers_.outer0 >> 6U) & 1U) << 17U);
}

unsigned Mapper376::prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (registers_.bank_select & 0x40U) != 0U;
    if (slot == 1U) {
        return registers_.banks[7U] & 7U;
    }
    if (slot == 3U) {
        return 7U;
    }
    return (slot == 0U) == swapped ? 6U : registers_.banks[6U] & 7U;
}

Byte Mapper376::chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot =
        ((pattern_addr >> 10U) & 7U) ^ ((registers_.bank_select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

Byte Mapper376::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto mode = registers_.outer0 & kOuterModeMask;
    if ((registers_.outer0 & 0x80U) != 0U && (mode == kNrom128Mode || mode == kNrom256Mode)) {
        const auto bank =
            static_cast<std::size_t>(registers_.outer0 & (mode == kNrom256Mode ? 6U : 7U));
        const auto offset =
            outer_offset() + bank * 0x4000UZ + (addr & (mode == kNrom128Mode ? 0x3FFFU : 0x7FFFU));
        return prg_rom_[offset % prg_rom_.size()];
    }
    const auto offset = outer_offset() + static_cast<std::size_t>(prg_bank(addr)) * kPrgBankSize +
                        (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper376::write_prg(Address addr, Byte value) {
    if ((addr & 0xF001U) == 0x7000U) {
        registers_.outer0 = value & 0xE7U;
        return;
    }
    if ((addr & 0xF001U) == 0x7001U) {
        registers_.outer1 = value & 1U;
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

bool Mapper376::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper376::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper376::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto offset = outer_offset() +
                        static_cast<std::size_t>(chr_bank(addr) & 0x7FU) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper376::write_chr(Address, Byte) {}

MirrorMode Mapper376::mirror_mode() const {
    return registers_.mirror;
}

void Mapper376::power_on() {
    reset();
}

void Mapper376::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

bool Mapper376::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper376::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper376::clock_irq_counter() {
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

std::uint16_t Mapper376::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper376::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer0);
    writer.write(registers_.outer1);
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

void Mapper376::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer0);
    reader.read(next.outer1);
    reader.read(next.bank_select);
    reader.read_array(next.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_latch);
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (next.outer0 & 0x18U) != 0U || next.outer1 > 1U ||
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
