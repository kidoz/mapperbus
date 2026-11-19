#include "core/mappers/mapper404.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::uint16_t kStateTag = 0x4040U;
constexpr Byte kOuterLock = 0x80U;
constexpr Byte kOuterMode = 0x40U;
constexpr Byte kOuterMask = 0xC7U;

bool valid_shift(Byte shift, Byte count) {
    if (count > 4U || shift > 0x1FU || (count == 0U && shift != 0x10U)) {
        return false;
    }
    const auto sentinel = static_cast<Byte>(0x10U >> count);
    return (shift & sentinel) != 0U && (shift & (sentinel - 1U)) == 0U;
}

} // namespace

// Original implementation from NESdev mapper 404 oldid 18976. The article only
// specifies outer-register power-on; clearing all registers on soft reset is a
// deterministic compatibility policy. This ROM-only board has no RAM to retain.
Mapper404::Mapper404(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> Mapper404::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 404U) {
        return std::unexpected("Mapper 404 requires mapper ID 404");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 404 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxRomSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxRomSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 404 requires 32 KiB-1 MiB PRG and 8 KiB-1 MiB CHR ROM in bank units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 404 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 404 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 404 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper404::prg_address(Address addr) const {
    const auto mode = (registers_.control >> 2U) & 3U;
    const auto selected = registers_.prg_bank & 0x0FU;
    const bool upper = addr >= 0xC000U;
    unsigned inner = 0U;
    switch (mode) {
    case 0U:
    case 1U:
        inner = (selected & 0x0EU) | static_cast<unsigned>(upper);
        break;
    case 2U:
        inner = upper ? selected : 0U;
        break;
    default:
        inner = upper ? 0x0FU : selected;
        break;
    }

    const auto outer = registers_.outer & 7U;
    const unsigned bank = (registers_.outer & kOuterMode) != 0U
                              ? (inner & 7U) | (static_cast<unsigned>(outer) << 3U)
                              : inner | (static_cast<unsigned>(outer & 6U) << 3U);
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
}

std::size_t Mapper404::chr_address(Address addr) const {
    const bool upper = (addr & 0x1000U) != 0U;
    unsigned inner = 0U;
    if ((registers_.control & 0x10U) != 0U) {
        inner = upper ? registers_.chr_bank1 : registers_.chr_bank0;
    } else {
        inner = (registers_.chr_bank0 & 0x1EU) | static_cast<unsigned>(upper);
    }
    const unsigned bank = inner | (static_cast<unsigned>(registers_.outer & 7U) << 5U);
    return static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x0FFFU);
}

Byte Mapper404::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_address(addr) % prg_rom_.size()];
}

void Mapper404::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (addr >= 0x6000U && (registers_.outer & kOuterLock) == 0U) {
            registers_.outer = value & kOuterMask;
        }
        return;
    }
    if ((value & 0x80U) != 0U) {
        registers_.shift = 0x10U;
        registers_.write_count = 0U;
        registers_.control |= 0x0CU;
        return;
    }

    registers_.shift = static_cast<Byte>((registers_.shift >> 1U) | ((value & 1U) << 4U));
    ++registers_.write_count;
    if (registers_.write_count == 5U) {
        write_register(addr, registers_.shift);
        registers_.shift = 0x10U;
        registers_.write_count = 0U;
    }
}

void Mapper404::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (addr < 0x8000U || !consecutive || (value & 0x80U) != 0U) {
        write_prg(addr, value);
    }
}

void Mapper404::write_register(Address addr, Byte value) {
    value &= 0x1FU;
    if (addr < 0xA000U) {
        registers_.control = value;
    } else if (addr < 0xC000U) {
        registers_.chr_bank0 = value;
    } else if (addr < 0xE000U) {
        registers_.chr_bank1 = value;
    } else {
        registers_.prg_bank = value;
    }
}

bool Mapper404::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper404::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper404::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_address(addr) % chr_rom_.size()];
}

void Mapper404::write_chr(Address, Byte) {}

MirrorMode Mapper404::mirror_mode() const {
    switch (registers_.control & 3U) {
    case 0U:
        return MirrorMode::SingleLower;
    case 1U:
        return MirrorMode::SingleUpper;
    case 2U:
        return MirrorMode::Vertical;
    default:
        return MirrorMode::Horizontal;
    }
}

void Mapper404::power_on() {
    reset();
}

void Mapper404::reset() {
    registers_ = Registers{};
}

void Mapper404::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.shift);
    writer.write(registers_.write_count);
    writer.write(registers_.control);
    writer.write(registers_.chr_bank0);
    writer.write(registers_.chr_bank1);
    writer.write(registers_.prg_bank);
}

void Mapper404::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.shift);
    reader.read(next.write_count);
    reader.read(next.control);
    reader.read(next.chr_bank0);
    reader.read(next.chr_bank1);
    reader.read(next.prg_bank);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (next.outer & ~kOuterMask) != 0U ||
        !valid_shift(next.shift, next.write_count) || next.control > 0x1FU ||
        next.chr_bank0 > 0x1FU || next.chr_bank1 > 0x1FU || next.prg_bank > 0x1FU) {
        reader.fail();
        return;
    }

    registers_ = next;
}

} // namespace mapperbus::core
