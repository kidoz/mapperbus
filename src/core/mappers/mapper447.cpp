#include "core/mappers/mapper447.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kPrgRamSize = 0x000800UZ;
constexpr std::uint16_t kStateTag = 0x4470U;
constexpr Byte kOuterBankMask = 0x03U;
constexpr Byte kOuterLock = 0x04U;
constexpr Byte kNromMode = 0x08U;
constexpr Byte kOuterMask = kOuterBankMask | kOuterLock | kNromMode;

} // namespace

// NESdev mapper 447 oldid 19301 leaves electrical reset values unspecified.
// Clearing VRC4/outer registers and volatile-only cold RAM clearing are policies.
Mapper447::Mapper447(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_size_(header.prg_ram_size + header.prg_nvram_size),
      nvram_size_(header.prg_nvram_size), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper447::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 447U) {
        return std::unexpected("Mapper 447 requires mapper ID 447");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 447 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 447 requires 32-512 KiB PRG in 8 KiB units and 8-512 KiB CHR in 1 KiB units");
    }
    if ((header.prg_ram_size != 0U && header.prg_ram_size != kPrgRamSize) ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kPrgRamSize) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
        header.has_battery != (header.prg_nvram_size != 0U) || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 447 supports no CPU RAM or one 2 KiB PRG RAM/NVRAM chip and CHR ROM only");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 447 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 447 does not provide trainer RAM");
    }
    return {};
}

bool Mapper447::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x7000U && prg_ram_size_ != 0U &&
                               (registers_.mode & 1U) != 0U);
}

Byte Mapper447::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper447::inner_prg_bank(Address addr) const {
    unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if ((registers_.outer & kNromMode) != 0U) {
        slot &= 1U;
    }
    if (slot == 1U) {
        return registers_.prg_banks[1U];
    }
    if (slot == 3U) {
        return 15U;
    }
    const bool swap = (registers_.mode & 2U) != 0U;
    return (slot == 0U) != swap ? registers_.prg_banks[0U] : 14U;
}

std::size_t Mapper447::prg_offset(Address addr) const {
    unsigned inner = inner_prg_bank(addr);
    if ((registers_.outer & (kNromMode | kOuterLock)) == kNromMode) {
        inner = (inner & ~2U) | ((addr >> 13U) & 2U);
    }
    const unsigned bank = (inner & 0x0FU) | ((registers_.outer & kOuterBankMask) << 4U);
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

Byte Mapper447::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return maps_prg(addr) ? prg_ram_[addr & 0x07FFU] : 0U;
    }
    return prg_rom_.empty() ? 0U : prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper447::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((registers_.outer & kOuterLock) == 0U) {
            registers_.outer = value & kOuterMask;
        }
        if (maps_prg(addr)) {
            prg_ram_[addr & 0x07FFU] = value;
        }
        return;
    }
    if (addr >= 0x8000U) {
        write_vrc4(addr, value);
    }
}

void Mapper447::write_vrc4(Address addr, Byte value) {
    const auto group = static_cast<unsigned>(addr) >> 12U;
    const auto port = static_cast<unsigned>((addr >> 2U) & 3U);
    if (group == 8U || group == 10U) {
        registers_.prg_banks[group == 8U ? 0U : 1U] = value & 0x1FU;
    } else if (group == 9U) {
        if (port == 0U) {
            constexpr std::array modes{MirrorMode::Vertical,
                                       MirrorMode::Horizontal,
                                       MirrorMode::SingleLower,
                                       MirrorMode::SingleUpper};
            registers_.mirror = modes[value & 3U];
        } else if (port == 2U) {
            registers_.mode = value & 3U;
        }
    } else if (group >= 11U && group <= 14U) {
        auto& bank = registers_.chr_banks[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U) {
            bank = (bank & 0x01F0U) | (value & 0x0FU);
        } else {
            bank = (bank & 0x000FU) | static_cast<std::uint16_t>((value & 0x1FU) << 4U);
        }
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

std::size_t Mapper447::chr_offset(Address addr) const {
    const unsigned inner = registers_.chr_banks[(addr >> 10U) & 7U];
    const unsigned bank = (inner & 0x7FU) | ((registers_.outer & kOuterBankMask) << 7U);
    return bank * kChrBankSize + (addr & 0x03FFU);
}

Byte Mapper447::read_chr(Address addr) {
    return chr_rom_.empty() ? 0U : chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper447::write_chr(Address, Byte) {}

MirrorMode Mapper447::mirror_mode() const {
    return registers_.mirror;
}

void Mapper447::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        prg_ram_.fill(0U);
    }
}

void Mapper447::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

void Mapper447::write_irq_control(Byte value) {
    registers_.irq_control = value & 7U;
    registers_.irq_enabled = (value & 2U) != 0U;
    registers_.irq_pending = false;
    registers_.irq_prescaler = 341U;
    if (registers_.irq_enabled) {
        registers_.irq_counter = registers_.irq_latch;
    }
}

void Mapper447::tick_irq() {
    if (registers_.irq_counter == 0xFFU) {
        registers_.irq_counter = registers_.irq_latch;
        registers_.irq_pending = true;
    } else {
        ++registers_.irq_counter;
    }
}

void Mapper447::clock_cpu() {
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

bool Mapper447::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper447::acknowledge_irq() {
    registers_.irq_pending = false;
}

std::uint16_t Mapper447::memory_tag() const {
    return static_cast<std::uint16_t>((initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (prg_ram_size_ != 0U ? 2U : 0U) |
                                      (nvram_size_ != 0U ? 4U : 0U));
}

void Mapper447::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_tag());
    writer.write(registers_.outer);
    writer.write_array(registers_.prg_banks);
    writer.write_array(registers_.chr_banks);
    writer.write(registers_.mirror);
    writer.write(registers_.mode);
    writer.write(registers_.irq_latch);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_prescaler);
    writer.write(registers_.irq_control);
    writer.write<Byte>((registers_.irq_enabled ? 1U : 0U) | (registers_.irq_pending ? 2U : 0U));
    if (prg_ram_size_ != 0U) {
        writer.write_array(prg_ram_);
    }
}

void Mapper447::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory = reader.read<std::uint16_t>();
    Registers next{};
    reader.read(next.outer);
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    reader.read(next.mirror);
    reader.read(next.mode);
    reader.read(next.irq_latch);
    reader.read(next.irq_counter);
    reader.read(next.irq_prescaler);
    reader.read(next.irq_control);
    const auto irq_flags = reader.read<Byte>();
    std::array<Byte, kPrgRamSize> prg_ram{};
    if (prg_ram_size_ != 0U) {
        reader.read_array(prg_ram);
    }

    const bool valid =
        reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
        chr_size == chr_rom_.size() && memory == memory_tag() && (next.outer & ~kOuterMask) == 0U &&
        std::ranges::all_of(next.prg_banks, [](Byte bank) { return bank <= 0x1FU; }) &&
        std::ranges::all_of(next.chr_banks, [](std::uint16_t bank) { return bank <= 0x01FFU; }) &&
        next.mirror != MirrorMode::FourScreen && next.mirror <= MirrorMode::SingleUpper &&
        next.mode <= 3U && next.irq_prescaler >= 1U && next.irq_prescaler <= 341U &&
        next.irq_control <= 7U && irq_flags <= 3U &&
        ((irq_flags & 2U) == 0U || (irq_flags & 1U) != 0U);
    if (!valid) {
        reader.fail();
        return;
    }

    next.irq_enabled = (irq_flags & 1U) != 0U;
    next.irq_pending = (irq_flags & 2U) != 0U;
    registers_ = next;
    if (prg_ram_size_ != 0U) {
        prg_ram_ = prg_ram;
    }
}

std::span<const Byte> Mapper447::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper447::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), nvram_size_), prg_ram_.begin());
}

} // namespace mapperbus::core
