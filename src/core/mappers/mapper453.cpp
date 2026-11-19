#include "core/mappers/mapper453.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kUnromBankSize = 0x004000UZ;
constexpr std::size_t kAoromBankSize = 0x008000UZ;
constexpr std::uint16_t kStateTag = 0x4530U;
constexpr Byte kBankMask = 0x07U;
constexpr Byte kMirrorMask = 0x10U;
constexpr Byte kModeMask = 0x20U;
constexpr Byte kOuterMask = 0x40U;
constexpr Byte kLockMask = kOuterMask | kModeMask;
constexpr Byte kLatchMask = kOuterMask | kModeMask | kMirrorMask | kBankMask;

} // namespace

// Original implementation from NESdev mapper 453 oldid 19315. Reset behavior is
// unspecified; clearing and unlocking the latch is a deterministic policy.
Mapper453::Mapper453(const INesHeader& /*header*/,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper453::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 453U) {
        return std::unexpected("Mapper 453 requires mapper ID 453");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 453 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 453 requires exactly 512 KiB PRG ROM and no CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 453 has no CPU work RAM or battery-backed RAM");
    }
    if (header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 453 requires exactly 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 453 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 453 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper453::prg_offset(Address addr) const {
    const unsigned outer = latch_ & kOuterMask;
    if ((latch_ & kModeMask) != 0U) {
        const unsigned bank = (outer >> 3U) | (latch_ & kBankMask);
        return bank * kAoromBankSize + (addr & (kAoromBankSize - 1U));
    }

    const unsigned inner = addr < 0xC000U ? (latch_ & kBankMask) : kBankMask;
    const unsigned bank = (outer >> 2U) | (outer >> 3U) | inner;
    return bank * kUnromBankSize + (addr & (kUnromBankSize - 1U));
}

Byte Mapper453::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    return prg_rom_.empty() ? 0U : prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper453::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if ((latch_ & kLockMask) == 0U) {
        latch_ = static_cast<Byte>((latch_ & ~kLockMask) | (value & kLockMask));
    }
    latch_ = static_cast<Byte>((latch_ & kLockMask) | (value & (kMirrorMask | kBankMask)));
}

bool Mapper453::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper453::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper453::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper453::write_chr(Address addr, Byte value) {
    chr_ram_[addr & (kChrRamSize - 1U)] = value;
}

MirrorMode Mapper453::mirror_mode() const {
    const bool selected = (latch_ & kMirrorMask) != 0U;
    if ((latch_ & kModeMask) != 0U) {
        return selected ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
    return selected ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper453::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper453::reset() {
    latch_ = 0U;
}

void Mapper453::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Mapper453::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid = reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
                       chr_size == 0U && (latch & ~kLatchMask) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    latch_ = latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
