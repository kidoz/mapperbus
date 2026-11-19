#include "core/mappers/mapper439.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::uint16_t kStateTag = 0x4390U;
constexpr Byte kLatchMask = 0x7FU;
constexpr Byte kOuterMask = 0x70U;
constexpr Byte kMaskMask = 0xF0U;
constexpr Byte kHorizontalMirror = 0x40U;
constexpr Byte kMirrorProtect = 0x80U;

} // namespace

// Original implementation from NES 2.0 mapper 439 oldid 21163. Soft reset clears
// the registers and retains CHR RAM; cold power also clears it deterministically.
Mapper439::Mapper439(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper439::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 439U) {
        return std::unexpected("Mapper 439 requires mapper ID 439");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 439 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 439 requires 32 KiB-1 MiB PRG ROM in 16 KiB units and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 439 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 439 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 439 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper439::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    unsigned bank = (addr & 0x4000U) == 0U ? registers_.latch & 0x3FU : 0x3FU;
    const unsigned outer_mask = (registers_.mask & kOuterMask) >> 1U;
    const unsigned outer = (registers_.outer & kOuterMask) >> 1U;
    bank = (bank & ~outer_mask) | (outer & outer_mask);
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper439::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        const Byte mirror = (registers_.mask & kMirrorProtect) != 0U
                                ? registers_.latch & kHorizontalMirror
                                : value & kHorizontalMirror;
        registers_.latch = static_cast<Byte>(mirror | (value & 0x3FU));
        return;
    }

    switch (addr & 0xE001U) {
    case 0x6000U:
        registers_.outer = value & kOuterMask;
        break;
    case 0x6001U:
        registers_.mask = value & kMaskMask;
        break;
    default:
        break;
    }
}

bool Mapper439::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper439::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper439::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper439::write_chr(Address addr, Byte value) {
    chr_ram_[addr & (kChrRamSize - 1U)] = value;
}

MirrorMode Mapper439::mirror_mode() const {
    return (registers_.latch & kHorizontalMirror) != 0U ? MirrorMode::Horizontal
                                                        : MirrorMode::Vertical;
}

void Mapper439::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper439::reset() {
    registers_ = Registers{};
}

void Mapper439::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.latch);
    writer.write(registers_.outer);
    writer.write(registers_.mask);
    writer.write_array(chr_ram_);
}

void Mapper439::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.latch);
    reader.read(next.outer);
    reader.read(next.mask);
    std::array<Byte, kChrRamSize> next_chr_ram{};
    reader.read_array(next_chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        (next.latch & ~kLatchMask) != 0U || (next.outer & ~kOuterMask) != 0U ||
        (next.mask & ~kMaskMask) != 0U) {
        reader.fail();
        return;
    }

    registers_ = next;
    chr_ram_ = next_chr_ram;
}

} // namespace mapperbus::core
