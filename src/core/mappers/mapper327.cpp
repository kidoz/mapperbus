#include "core/mappers/mapper327.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kOuterBankSize = 128U * 1024U;
constexpr std::size_t kMaxRomSize = 8U * kOuterBankSize;
constexpr std::size_t kCartridgeRamSize = 8192U;
constexpr std::uint16_t kStateTag = 0x3270U;

constexpr Byte kIrqReloadPending = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

} // namespace

Mapper327::Mapper327(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirroring_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper327::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 327U || !header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 327 requires NES 2.0 mapper 327 submapper 0");
    }
    if (prg_size < kOuterBankSize || prg_size > kMaxRomSize || prg_size % kOuterBankSize != 0U ||
        chr_size < kOuterBankSize || chr_size > kMaxRomSize || chr_size % kOuterBankSize != 0U) {
        return std::unexpected(
            "Mapper 327 requires 128 KiB-aligned PRG and CHR ROM from 128 KiB through 1 MiB");
    }
    if (header.has_battery || header.prg_ram_size != kCartridgeRamSize ||
        header.prg_nvram_size != 0U || header.chr_ram_size != kCartridgeRamSize ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 327 requires 8 KiB volatile WRAM and 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 327 requires ordinary H/V nametable metadata");
    }
    return {};
}

bool Mapper327::ram_enabled() const {
    return (registers_.ram_flags & 0x80U) != 0U;
}

bool Mapper327::ram_write_protected() const {
    return (registers_.ram_flags & 0x40U) != 0U;
}

unsigned Mapper327::prg_inner_mask() const {
    return (registers_.outer & 0x08U) != 0U ? 31U : 15U;
}

unsigned Mapper327::chr_inner_mask() const {
    return (registers_.outer & 0x20U) != 0U ? 255U : 127U;
}

unsigned Mapper327::prg_bank(unsigned slot) const {
    const unsigned mask = prg_inner_mask();
    unsigned inner = registers_.banks[7U];
    if (slot == 0U) {
        inner = (registers_.bank_mode & 0x40U) != 0U ? mask - 1U : registers_.banks[6U];
    } else if (slot == 2U) {
        inner = (registers_.bank_mode & 0x40U) != 0U ? registers_.banks[6U] : mask - 1U;
    } else if (slot == 3U) {
        inner = mask;
    }
    return (inner & mask) | ((registers_.outer & 7U) << 4U);
}

unsigned Mapper327::chr_bank(Address addr) const {
    const auto slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^
                      ((registers_.bank_mode & 0x80U) != 0U ? 4U : 0U);
    const unsigned inner = slot < 4U ? (registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U)
                                     : registers_.banks[slot - 2U];
    return (inner & chr_inner_mask()) | ((registers_.outer & 7U) << 7U);
}

Byte Mapper327::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return ram_enabled() ? wram_[addr - 0x6000U] : 0U;
    }
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto slot = static_cast<unsigned>(addr - 0x8000U) >> 13U;
    const auto offset = static_cast<std::size_t>(prg_bank(slot)) * 8192U + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper327::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (ram_enabled() && !ram_write_protected()) {
            wram_[addr - 0x6000U] = value;
            if (registers_.outer_locked == 0U) {
                registers_.outer = value & 0x3FU;
                registers_.outer_locked = (registers_.outer & 7U) != 0U ? 1U : 0U;
            }
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

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
        registers_.mirroring =
            static_cast<Byte>((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical);
        break;
    case 0xA001U:
        registers_.ram_flags = value & 0xC0U;
        break;
    case 0xC000U:
        registers_.irq_reload = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_flags |= kIrqReloadPending;
        break;
    case 0xE000U:
        registers_.irq_flags &= kIrqReloadPending;
        break;
    case 0xE001U:
        registers_.irq_flags |= kIrqEnabled;
        break;
    default:
        break;
    }
}

bool Mapper327::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U && ram_enabled());
}

Byte Mapper327::read_chr(Address addr) {
    if ((registers_.outer & 0x10U) != 0U) {
        return chr_ram_[addr & 0x1FFFU];
    }
    const auto offset = static_cast<std::size_t>(chr_bank(addr)) * 1024U + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper327::write_chr(Address addr, Byte value) {
    if ((registers_.outer & 0x10U) != 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper327::mirror_mode() const {
    return static_cast<MirrorMode>(registers_.mirroring);
}

void Mapper327::power_on() {
    registers_ = Registers{};
    reset();
    wram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper327::reset() {
    const auto outer = registers_.outer;
    const auto outer_locked = registers_.outer_locked;
    registers_ = Registers{};
    registers_.outer = outer;
    registers_.outer_locked = outer_locked;
    registers_.mirroring = static_cast<Byte>(initial_mirroring_);
}

bool Mapper327::irq_pending() const {
    return (registers_.irq_flags & kIrqPending) != 0U;
}

void Mapper327::acknowledge_irq() {
    registers_.irq_flags &= ~kIrqPending;
}

void Mapper327::clock_irq_counter() {
    if (registers_.irq_counter == 0U || (registers_.irq_flags & kIrqReloadPending) != 0U) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_flags &= ~kIrqReloadPending;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & kIrqEnabled) != 0U) {
        registers_.irq_flags |= kIrqPending;
    }
}

std::uint16_t Mapper327::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirroring_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper327::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write<std::uint32_t>(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.outer_locked);
    writer.write(registers_.bank_select);
    writer.write(registers_.bank_mode);
    writer.write_array(registers_.banks);
    writer.write(registers_.mirroring);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write(registers_.irq_flags);
    writer.write(registers_.ram_flags);
    writer.write_array(wram_);
    writer.write_array(chr_ram_);
}

void Mapper327::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.outer_locked);
    reader.read(next.bank_select);
    reader.read(next.bank_mode);
    reader.read_array(next.banks);
    reader.read(next.mirroring);
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    reader.read(next.irq_flags);
    reader.read(next.ram_flags);
    std::array<Byte, kCartridgeRamSize> wram{};
    std::array<Byte, kCartridgeRamSize> chr_ram{};
    reader.read_array(wram);
    reader.read_array(chr_ram);

    const bool valid_outer = next.outer <= 0x3FU && next.outer_locked <= 1U &&
                             ((next.outer & 7U) != 0U) == (next.outer_locked != 0U);
    const bool valid_mirror = next.mirroring == static_cast<Byte>(MirrorMode::Horizontal) ||
                              next.mirroring == static_cast<Byte>(MirrorMode::Vertical);
    const bool valid_irq = next.irq_flags <= 7U && ((next.irq_flags & kIrqPending) == 0U ||
                                                    (next.irq_flags & kIrqEnabled) != 0U);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || !valid_outer || next.bank_select > 7U ||
        (next.bank_mode & 0x3FU) != 0U || next.banks[6U] > 0x3FU || next.banks[7U] > 0x3FU ||
        !valid_mirror || !valid_irq || (next.ram_flags & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    registers_ = next;
    wram_ = wram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
