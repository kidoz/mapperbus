#include "core/mappers/mapper385.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kMinPrgSize = kPrgBankSize;
constexpr std::size_t kMaxPrgSize = 8UZ * kPrgBankSize;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3850U;
constexpr Byte kLatchMask = 0x0FU;
constexpr Byte kHorizontalMirroring = 0x01U;

} // namespace

// Original implementation from NESdev mapper 385 oldid 7999. Reset behavior is
// unspecified; clearing the latch and retaining CHR on soft reset are deterministic
// compatibility policies. Cold power also clears the volatile CHR RAM.
Mapper385::Mapper385(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper385::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 385U) {
        return std::unexpected("Mapper 385 requires mapper ID 385");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 385 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 385 requires 16-128 KiB PRG ROM in 16 KiB units and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 385 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 385 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper385::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const auto bank = static_cast<std::size_t>((address_latch_ >> 1U) & 7U);
    const auto offset = bank * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper385::write_prg(Address addr, Byte) {
    if (addr >= 0x8000U) {
        address_latch_ = static_cast<Byte>(addr) & kLatchMask;
    }
}

bool Mapper385::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper385::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper385::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper385::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper385::mirror_mode() const {
    return (address_latch_ & kHorizontalMirroring) != 0U ? MirrorMode::Horizontal
                                                         : MirrorMode::Vertical;
}

void Mapper385::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper385::reset() {
    address_latch_ = 0U;
}

void Mapper385::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(address_latch_);
    writer.write_array(chr_ram_);
}

void Mapper385::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto address_latch = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        (address_latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }

    address_latch_ = address_latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
