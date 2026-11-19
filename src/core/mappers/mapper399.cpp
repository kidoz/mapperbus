#include "core/mappers/mapper399.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::uint16_t kStateTag = 0x3990U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

} // namespace

// Original implementation from NESdev mapper 399 oldid 24616. Reset behavior and
// PRG RAM are unspecified; deterministic register reset, no CPU RAM, volatile CHR
// RAM retained on soft reset, and the core's MMC3 IRQ profile are compatibility policies.
Mapper399::Mapper399(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper399::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 399U) {
        return std::unexpected("Mapper 399 requires mapper ID 399");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 399 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 399 requires exactly 1 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 399 requires only 32 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 399 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 399 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper399::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned bank = slot == 0U   ? 0U
                          : slot == 3U ? 127U
                                       : registers_.prg_banks[slot - 1U] & 0x7FU;
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper399::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.chr_banks[(value & 0x80U) != 0U ? 1U : 0U] = value & 7U;
        break;
    case 0x8001U:
        registers_.prg_banks[(value & 0x80U) != 0U ? 1U : 0U] = value & 0x7FU;
        break;
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
        registers_.irq_flags |= kIrqReload;
        break;
    case 0xE000U:
        registers_.irq_flags &= kIrqReload;
        break;
    case 0xE001U:
        registers_.irq_flags |= kIrqEnabled;
        break;
    default:
        break;
    }
}

bool Mapper399::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper399::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper399::chr_offset(Address addr) const {
    const unsigned slot = (addr & 0x1000U) != 0U ? 1U : 0U;
    return static_cast<std::size_t>(registers_.chr_banks[slot]) * kChrBankSize + (addr & 0x0FFFU);
}

Byte Mapper399::read_chr(Address addr) {
    return chr_ram_[chr_offset(addr)];
}

void Mapper399::write_chr(Address addr, Byte value) {
    chr_ram_[chr_offset(addr)] = value;
}

MirrorMode Mapper399::mirror_mode() const {
    return registers_.mirror;
}

void Mapper399::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper399::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

bool Mapper399::irq_pending() const {
    return (registers_.irq_flags & kIrqPending) != 0U;
}

void Mapper399::acknowledge_irq() {
    registers_.irq_flags &= ~kIrqPending;
}

void Mapper399::clock_irq_counter() {
    if (registers_.irq_counter == 0U || (registers_.irq_flags & kIrqReload) != 0U) {
        registers_.irq_counter = registers_.irq_latch;
        registers_.irq_flags &= ~kIrqReload;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & kIrqEnabled) != 0U) {
        registers_.irq_flags |= kIrqPending;
    }
}

std::uint16_t Mapper399::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper399::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write_array(registers_.prg_banks);
    writer.write_array(registers_.chr_banks);
    writer.write(static_cast<Byte>(registers_.mirror));
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_latch);
    writer.write(registers_.irq_flags);
    writer.write_array(chr_ram_);
}

void Mapper399::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_latch);
    reader.read(next.irq_flags);
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid_banks = next.prg_banks[0U] <= 0x7FU && next.prg_banks[1U] <= 0x7FU &&
                             next.chr_banks[0U] <= 7U && next.chr_banks[1U] <= 7U;
    const bool valid_irq = next.irq_flags <= 7U && ((next.irq_flags & kIrqPending) == 0U ||
                                                    (next.irq_flags & kIrqEnabled) != 0U);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        !valid_banks || mirror > 1U || !valid_irq) {
        reader.fail();
        return;
    }

    next.mirror = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    registers_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
