#include "core/mappers/mapper431.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4310U;
constexpr Byte kOuterMask = 0x67U;
constexpr Byte kInnerMask = 0x07U;

} // namespace

// Original implementation from NESdev mapper 431 oldid 23621 and its mapper 396
// equivalence. Reset clearing and cold-only CHR RAM clearing are deterministic policies.
Mapper431::Mapper431(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), submapper_(header.submapper),
      initial_mirror_(header.mirror_mode) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper431::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 431U) {
        return std::unexpected("Mapper 431 requires mapper ID 431");
    }
    if (!header.is_nes2 || header.submapper > 3U) {
        return std::unexpected("Mapper 431 requires NES 2.0 submapper 0 through 3");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 431 requires 32 KiB-1 MiB PRG ROM in 16 KiB units");
    }
    if (chr_size != 0U) {
        return std::unexpected("Mapper 431 requires no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 431 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 431 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 431 does not provide trainer RAM");
    }
    return {};
}

bool Mapper431::is_outer_register(Address addr) const {
    if (submapper_ == 3U) {
        return (addr & 0xC000U) == 0x8000U;
    }
    return (addr & 0xE000U) == 0xA000U;
}

Byte Mapper431::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const unsigned inner = (addr & 0x4000U) == 0U ? registers_.inner : 7U;
    const unsigned bank = ((registers_.outer & 7U) << 3U) | inner;
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper431::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if (is_outer_register(addr)) {
        registers_.outer = value & kOuterMask;
    } else {
        registers_.inner = value & kInnerMask;
    }
}

bool Mapper431::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper431::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper431::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper431::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper431::mirror_mode() const {
    const Byte mask = submapper_ == 1U ? 0x40U : submapper_ == 0U ? 0x60U : 0x20U;
    return (registers_.outer & mask) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

void Mapper431::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper431::reset() {
    registers_ = Registers{};
}

std::uint16_t Mapper431::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | submapper_ |
                                      (initial_mirror_ == MirrorMode::Vertical ? 0x8000U : 0U));
}

void Mapper431::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.outer);
    writer.write(registers_.inner);
    writer.write_array(chr_ram_);
}

void Mapper431::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.inner);
    std::array<Byte, kChrRamSize> next_chr_ram{};
    reader.read_array(next_chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        (next.outer & ~kOuterMask) != 0U || (next.inner & ~kInnerMask) != 0U) {
        reader.fail();
        return;
    }

    registers_ = next;
    chr_ram_ = next_chr_ram;
}

} // namespace mapperbus::core
