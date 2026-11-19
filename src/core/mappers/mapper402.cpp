#include "core/mappers/mapper402.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4020U;
constexpr std::uint16_t kLatchMask = 0x0CDFU;
constexpr std::uint16_t kLowPrgEnable = 0x0800U;
constexpr std::uint16_t kChrWriteEnable = 0x0400U;
constexpr std::uint16_t kHorizontalMirroring = 0x0080U;
constexpr std::uint16_t kNrom128Mode = 0x0040U;

} // namespace

// Original implementation from NESdev mapper 402 oldid 18959. Electrical reset is
// unspecified: a zeroed latch and soft-reset CHR retention are deterministic policies.
Mapper402::Mapper402(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper402::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 402U) {
        return std::unexpected("Mapper 402 requires mapper ID 402");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 402 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 402 requires exactly 512 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 402 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 402 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 402 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper402::prg_offset(Address addr) const {
    unsigned bank = latch_ & 0x1FU;
    if ((latch_ & kNrom128Mode) == 0U) {
        bank = (bank & 0x1EU) | ((addr & 0x4000U) != 0U ? 1U : 0U);
    }
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
}

Byte Mapper402::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_.empty()) {
        return 0U;
    }
    const auto mapped = static_cast<Address>(addr < 0x8000U ? addr + 0x8000U : addr);
    return prg_rom_[prg_offset(mapped) % prg_rom_.size()];
}

void Mapper402::write_prg(Address addr, Byte) {
    if (addr < 0x8000U) {
        if (!maps_prg(addr)) {
            return;
        }
        addr = static_cast<Address>(addr + 0x8000U);
    }
    latch_ = addr & kLatchMask;
}

bool Mapper402::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U && (latch_ & kLowPrgEnable) != 0U);
}

Byte Mapper402::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper402::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper402::write_chr(Address addr, Byte value) {
    if ((latch_ & kChrWriteEnable) != 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper402::mirror_mode() const {
    return (latch_ & kHorizontalMirroring) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper402::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper402::reset() {
    latch_ = 0U;
}

void Mapper402::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Mapper402::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<std::uint16_t>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        (latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }

    latch_ = latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
