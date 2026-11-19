#include "core/mappers/mapper377.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x020000UZ;
constexpr std::uint16_t kStateTag = 0x7700U;
constexpr Byte kOuterBankMask = 0x26U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

} // namespace

// Original implementation from NESdev mapper 377 oldid 7987 and its mapper
// 267 base. Only PRG uses the outer bank; CHR retains the 128 KiB inner space.
// Reset defaults and repeated zero-latch IRQs follow the core's MMC3 policy.
Mapper377::Mapper377(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper377::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 377U) {
        return std::unexpected("Mapper 377 requires mapper ID 377");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 377 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 377 requires 32 KiB-2 MiB PRG and 8-128 KiB CHR ROM in bank units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 377 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 377 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

unsigned Mapper377::outer_bank() const {
    return ((registers_.outer_bank & 0x20U) >> 3U) | ((registers_.outer_bank & 0x06U) >> 1U);
}

unsigned Mapper377::prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (registers_.bank_select & 0x40U) != 0U;
    unsigned inner = 31U;
    if (slot == 0U) {
        inner = swapped ? 30U : registers_.banks[6U] & 0x1FU;
    } else if (slot == 1U) {
        inner = registers_.banks[7U] & 0x1FU;
    } else if (slot == 2U) {
        inner = swapped ? registers_.banks[6U] & 0x1FU : 30U;
    }
    return inner | (outer_bank() << 5U);
}

Byte Mapper377::chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((registers_.bank_select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0x7EU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U] & 0x7FU;
}

Byte Mapper377::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto offset =
        static_cast<std::size_t>(prg_bank(addr)) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper377::write_prg(Address addr, Byte value) {
    if (addr == 0x6000U) {
        registers_.outer_bank = value & kOuterBankMask;
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
        registers_.banks[index] = value & (index >= 6U ? 0x1FU : 0x7FU);
        break;
    }
    case 0xA000U:
        registers_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
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

bool Mapper377::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper377::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper377::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto offset =
        static_cast<std::size_t>(chr_bank(addr)) * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper377::write_chr(Address, Byte) {}

MirrorMode Mapper377::mirror_mode() const {
    return registers_.mirror;
}

void Mapper377::power_on() {
    reset();
}

void Mapper377::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

bool Mapper377::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper377::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper377::clock_irq_counter() {
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

std::uint16_t Mapper377::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper377::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer_bank);
    writer.write(registers_.bank_select);
    writer.write_array(registers_.banks);
    writer.write<Byte>(registers_.mirror == MirrorMode::Vertical ? 1U : 0U);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_latch);
    writer.write<Byte>((registers_.irq_reload ? kIrqReload : 0U) |
                       (registers_.irq_enabled ? kIrqEnabled : 0U) |
                       (registers_.irq_pending ? kIrqPending : 0U));
}

void Mapper377::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer_bank);
    reader.read(next.bank_select);
    reader.read_array(next.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_latch);
    const auto irq_flags = reader.read<Byte>();

    bool valid_banks = (next.banks[6U] & 0xE0U) == 0U && (next.banks[7U] & 0xE0U) == 0U;
    for (unsigned index = 0U; index < 6U; ++index) {
        valid_banks = valid_banks && (next.banks[index] & 0x80U) == 0U;
    }
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (next.outer_bank & ~kOuterBankMask) != 0U ||
        (next.bank_select & 0x38U) != 0U || !valid_banks || mirror > 1U || irq_flags > 7U ||
        ((irq_flags & kIrqPending) != 0U && (irq_flags & kIrqEnabled) == 0U)) {
        reader.fail();
        return;
    }

    next.mirror = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    next.irq_reload = (irq_flags & kIrqReload) != 0U;
    next.irq_enabled = (irq_flags & kIrqEnabled) != 0U;
    next.irq_pending = (irq_flags & kIrqPending) != 0U;
    registers_ = next;
}

} // namespace mapperbus::core
