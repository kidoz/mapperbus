#include "core/mappers/mapper437.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::uint16_t kStateTag = 0x4370U;
constexpr Byte kOuterMask = 0x0FU;
constexpr Byte kInnerMask = 0x07U;
constexpr Byte kHorizontalMirror = 0x08U;

} // namespace

// Original implementation from NES 2.0 mapper 437 oldid 19230 and UNROM banking.
// Reset clears both latches deterministically; soft reset retains volatile CHR RAM.
Mapper437::Mapper437(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper437::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 437U) {
        return std::unexpected("Mapper 437 requires mapper ID 437");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 437 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 437 requires 1 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 437 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 437 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 437 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper437::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const unsigned outer = registers_.outer & 7U;
    const unsigned inner = (addr & 0x4000U) == 0U ? registers_.inner & kInnerMask : 7U;
    const auto offset = ((outer << 3U) | inner) * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper437::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        registers_.inner = value & read_prg(addr) & kInnerMask;
    }
}

bool Mapper437::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper437::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper437::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper437::write_chr(Address addr, Byte value) {
    chr_ram_[addr & (kChrRamSize - 1U)] = value;
}

MirrorMode Mapper437::mirror_mode() const {
    return (registers_.outer & kHorizontalMirror) != 0U ? MirrorMode::Horizontal
                                                        : MirrorMode::Vertical;
}

bool Mapper437::maps_expansion(Address addr) const {
    return (addr & 0xF000U) == 0x5000U;
}

Byte Mapper437::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0U : 0xFFU;
}

void Mapper437::write_expansion(Address addr, Byte) {
    if (maps_expansion(addr)) {
        registers_.outer = static_cast<Byte>(addr) & kOuterMask;
    }
}

void Mapper437::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper437::reset() {
    registers_ = Registers{};
}

void Mapper437::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.outer);
    writer.write(registers_.inner);
    writer.write_array(chr_ram_);
}

void Mapper437::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.inner);
    std::array<Byte, kChrRamSize> next_chr_ram{};
    reader.read_array(next_chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        (next.outer & ~kOuterMask) != 0U || (next.inner & ~kInnerMask) != 0U) {
        reader.fail();
        return;
    }

    registers_ = next;
    chr_ram_ = next_chr_ram;
}

} // namespace mapperbus::core
