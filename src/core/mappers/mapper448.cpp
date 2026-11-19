#include "core/mappers/mapper448.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4480U;
constexpr Byte kOuterMask = 0x0FU;
constexpr Byte kAoromMode = 0x08U;
constexpr Byte kUoromSelect = 0x04U;
constexpr Byte kPrgBankMask = 0x1FU;

} // namespace

// NESdev mapper 448 oldid 19305 leaves electrical reset values unspecified.
// Cleared outer/VRC4 registers and volatile-only cold CHR clearing are policies.
Mapper448::Mapper448(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper448::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 448U) {
        return std::unexpected("Mapper 448 requires mapper ID 448");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 448 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 448 requires 32 KiB-2 MiB PRG ROM in 8 KiB units");
    }
    if (chr_size != 0U || header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 448 requires 8 KiB volatile CHR RAM and no CHR ROM/NVRAM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 448 has no CPU RAM, NVRAM or battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 448 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 448 does not provide trainer RAM");
    }
    return {};
}

bool Mapper448::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper448::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper448::prg_offset(Address addr) const {
    const unsigned outer = registers_.outer;
    if ((outer & kAoromMode) != 0U) {
        const unsigned bank = ((outer & 3U) << 3U) | (registers_.prg_bank & 7U);
        return bank * 0x8000UZ + (addr & 0x7FFFU);
    }
    const unsigned fixed = (outer & kUoromSelect) != 0U ? 15U : 7U;
    const unsigned inner = addr < 0xC000U ? registers_.prg_bank & 0x0FU : fixed;
    const unsigned bank = ((outer & 7U) << 4U) | inner;
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

Byte Mapper448::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    return prg_rom_.empty() ? 0U : prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper448::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        registers_.outer = addr & kOuterMask;
        return;
    }
    if (addr >= 0x8000U) {
        write_vrc4(addr, value);
    }
}

void Mapper448::write_vrc4(Address addr, Byte value) {
    const auto group = static_cast<unsigned>(addr) >> 12U;
    const auto port = static_cast<unsigned>((addr >> 2U) & 3U);
    if (group == 8U) {
        registers_.prg_bank = value & kPrgBankMask;
    } else if (group == 9U && port == 0U) {
        constexpr std::array modes{MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
        registers_.mirror = modes[value & 3U];
    } else if (group == 15U) {
        switch (port) {
        case 0U:
            registers_.irq_latch = (registers_.irq_latch & 0xF0U) | (value & 0x0FU);
            break;
        case 1U:
            registers_.irq_latch =
                (registers_.irq_latch & 0x0FU) | static_cast<Byte>((value & 0x0FU) << 4U);
            break;
        case 2U:
            write_irq_control(value);
            break;
        case 3U:
            registers_.irq_pending = false;
            registers_.irq_enabled = (registers_.irq_control & 1U) != 0U;
            break;
        default:
            std::unreachable();
        }
    }
}

Byte Mapper448::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper448::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper448::mirror_mode() const {
    if ((registers_.outer & kAoromMode) != 0U) {
        return (registers_.prg_bank & 0x10U) != 0U ? MirrorMode::SingleUpper
                                                   : MirrorMode::SingleLower;
    }
    return registers_.mirror;
}

void Mapper448::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper448::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

void Mapper448::write_irq_control(Byte value) {
    registers_.irq_control = value & 7U;
    registers_.irq_enabled = (value & 2U) != 0U;
    registers_.irq_pending = false;
    registers_.irq_prescaler = 341U;
    if (registers_.irq_enabled) {
        registers_.irq_counter = registers_.irq_latch;
    }
}

void Mapper448::tick_irq() {
    if (registers_.irq_counter == 0xFFU) {
        registers_.irq_counter = registers_.irq_latch;
        registers_.irq_pending = true;
    } else {
        ++registers_.irq_counter;
    }
}

void Mapper448::clock_cpu() {
    if (!registers_.irq_enabled) {
        return;
    }
    if ((registers_.irq_control & 4U) != 0U) {
        tick_irq();
    } else if (registers_.irq_prescaler <= 3U) {
        registers_.irq_prescaler += 338U;
        tick_irq();
    } else {
        registers_.irq_prescaler -= 3U;
    }
}

bool Mapper448::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper448::acknowledge_irq() {
    registers_.irq_pending = false;
}

std::uint16_t Mapper448::memory_tag() const {
    return initial_mirror_ == MirrorMode::Vertical ? 1U : 0U;
}

void Mapper448::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(kChrRamSize);
    writer.write(memory_tag());
    writer.write(registers_.outer);
    writer.write(registers_.prg_bank);
    writer.write(registers_.mirror);
    writer.write(registers_.irq_latch);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_prescaler);
    writer.write(registers_.irq_control);
    writer.write<Byte>((registers_.irq_enabled ? 1U : 0U) | (registers_.irq_pending ? 2U : 0U));
    writer.write_array(chr_ram_);
}

void Mapper448::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory = reader.read<std::uint16_t>();
    Registers next{};
    reader.read(next.outer);
    reader.read(next.prg_bank);
    reader.read(next.mirror);
    reader.read(next.irq_latch);
    reader.read(next.irq_counter);
    reader.read(next.irq_prescaler);
    reader.read(next.irq_control);
    const auto irq_flags = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid = reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
                       chr_size == kChrRamSize && memory == memory_tag() &&
                       (next.outer & ~kOuterMask) == 0U && next.prg_bank <= kPrgBankMask &&
                       next.mirror != MirrorMode::FourScreen &&
                       next.mirror <= MirrorMode::SingleUpper && next.irq_prescaler >= 1U &&
                       next.irq_prescaler <= 341U && next.irq_control <= 7U && irq_flags <= 3U &&
                       ((irq_flags & 2U) == 0U || (irq_flags & 1U) != 0U);
    if (!valid) {
        reader.fail();
        return;
    }

    next.irq_enabled = (irq_flags & 1U) != 0U;
    next.irq_pending = (irq_flags & 2U) != 0U;
    registers_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
