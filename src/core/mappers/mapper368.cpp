#include "core/mappers/mapper368.hpp"

#include <array>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x020000UZ;
constexpr std::size_t kChrSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr Address kRegisterMask = 0xF1FFU;
constexpr Address kPrgRegister = 0x4022U;
constexpr Address kIrqRegister = 0x4122U;
constexpr std::uint16_t kIrqCounterMask = 0x0FFFU;
constexpr std::uint16_t kStateTag = 0x3680U;
constexpr Byte kLatchMask = 0x35U;
constexpr Byte kLatchReadBits = 0x8AU;
constexpr Byte kIrqEnabled = 1U;
constexpr Byte kIrqPending = 2U;
constexpr std::array<Byte, 8> kPrgBanks{4U, 3U, 5U, 3U, 6U, 3U, 7U, 3U};

bool is_prg_register(Address addr) {
    return (addr & kRegisterMask) == kPrgRegister;
}

bool is_irq_register(Address addr) {
    return (addr & kRegisterMask) == kIrqRegister;
}

} // namespace

// NESdev mapper 368 oldid 7973 defines the ROM layout, register decoding, latch,
// and IRQ. Zeroed reset state is a deterministic policy; the article establishes
// no electrical reset values, cartridge RAM, or expansion audio.
Mapper368::Mapper368(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper368::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 368U) {
        return std::unexpected("Mapper 368 requires mapper ID 368");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 368 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != kChrSize) {
        return std::unexpected("Mapper 368 requires exactly 128 KiB PRG and 8 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 368 requires ROM-only media without RAM, NVRAM, or battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 368 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper368::read_prg(Address addr) {
    if (addr < 0x6000U) {
        return 0U;
    }

    unsigned bank = 8U;
    if (addr < 0x8000U) {
        bank = 2U;
    } else if (addr < 0xA000U) {
        bank = 1U;
    } else if (addr < 0xC000U) {
        bank = 0U;
    } else if (addr < 0xE000U) {
        bank = kPrgBanks[registers_.prg_bank];
    }
    const auto offset =
        static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset];
}

void Mapper368::write_prg(Address, Byte) {}

bool Mapper368::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper368::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper368::read_chr(Address addr) {
    return chr_rom_[addr & 0x1FFFU];
}

void Mapper368::write_chr(Address, Byte) {}

MirrorMode Mapper368::mirror_mode() const {
    return initial_mirror_;
}

bool Mapper368::maps_expansion(Address addr) const {
    return is_prg_register(addr) || is_irq_register(addr);
}

Byte Mapper368::expansion_read_mask(Address addr) const {
    return is_irq_register(addr) ? 0xFFU : 0U;
}

Byte Mapper368::read_expansion(Address addr) {
    return is_irq_register(addr) ? static_cast<Byte>(kLatchReadBits | registers_.latch) : 0U;
}

void Mapper368::write_expansion(Address addr, Byte value) {
    if (is_prg_register(addr)) {
        registers_.prg_bank = value & 7U;
        return;
    }
    if (!is_irq_register(addr)) {
        return;
    }

    registers_.latch = value & kLatchMask;
    registers_.irq_enabled = (value & 1U) != 0U;
    if (!registers_.irq_enabled) {
        registers_.irq_counter = 0U;
        registers_.irq_pending = false;
    }
}

void Mapper368::power_on() {
    reset();
}

void Mapper368::reset() {
    registers_ = Registers{};
}

bool Mapper368::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper368::clock_cpu() {
    if (!registers_.irq_enabled) {
        return;
    }
    if (registers_.irq_counter == kIrqCounterMask) {
        registers_.irq_counter = 0U;
        registers_.irq_pending = true;
    } else {
        ++registers_.irq_counter;
    }
}

std::uint16_t Mapper368::memory_state_tag() const {
    return initial_mirror_ == MirrorMode::Vertical ? 1U : 0U;
}

void Mapper368::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_state_tag());
    writer.write(registers_.prg_bank);
    writer.write(registers_.latch);
    writer.write(registers_.irq_counter);
    writer.write<Byte>((registers_.irq_enabled ? kIrqEnabled : 0U) |
                       (registers_.irq_pending ? kIrqPending : 0U));
}

void Mapper368::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory_tag = reader.read<std::uint16_t>();
    Registers next;
    reader.read(next.prg_bank);
    reader.read(next.latch);
    reader.read(next.irq_counter);
    const auto irq_flags = reader.read<Byte>();

    const bool enabled = (irq_flags & kIrqEnabled) != 0U;
    const bool pending = (irq_flags & kIrqPending) != 0U;
    const bool valid_irq = next.irq_counter <= kIrqCounterMask && irq_flags <= 3U &&
                           enabled == ((next.latch & 1U) != 0U) && (!pending || enabled) &&
                           (enabled || next.irq_counter == 0U);
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory_tag != memory_state_tag() || next.prg_bank > 7U ||
        (next.latch & ~kLatchMask) != 0U || !valid_irq) {
        reader.fail();
        return;
    }

    next.irq_enabled = enabled;
    next.irq_pending = pending;
    registers_ = next;
}

} // namespace mapperbus::core
