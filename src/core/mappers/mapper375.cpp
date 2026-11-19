#include "core/mappers/mapper375.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr Address kAddressLatchMask = 0x0FFFU;
constexpr Address kLockBit = 0x0800U;
constexpr Address kOuterModeBit = 0x0080U;
constexpr std::uint16_t kStateTag = 0x7500U;

} // namespace

// Original implementation from NESdev mapper 375 oldid 23353. Soft reset clears
// both latches and retains CHR RAM; cold power additionally clears it. The
// article specifies the latch reset but not analog RAM initialization.
Mapper375::Mapper375(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper375::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 375U) {
        return std::unexpected("Mapper 375 requires mapper ID 375");
    }
    if (header.is_nes2 && header.submapper != 0U) {
        return std::unexpected("Mapper 375 supports only NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 375 requires exactly 2 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U))) {
        return std::unexpected("Mapper 375 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 375 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

unsigned Mapper375::mapped_prg_bank(Address addr) const {
    const bool upper = addr >= 0xC000U;
    const auto address_inner = static_cast<unsigned>((address_latch_ >> 2U) & 7U);
    unsigned inner = (address_latch_ & kLockBit) != 0U && !upper ? data_latch_ : address_inner;
    if (upper && (address_latch_ & kOuterModeBit) == 0U) {
        inner = (address_latch_ & 0x0200U) != 0U ? 7U : 0U;
    }
    if ((address_latch_ & 1U) != 0U) {
        inner = (inner & 6U) | static_cast<unsigned>(upper);
    }
    const unsigned outer = ((address_latch_ >> 5U) & 3U) | ((address_latch_ >> 6U) & 4U) |
                           ((address_latch_ >> 7U) & 8U);
    return (outer << 3U) | inner;
}

Byte Mapper375::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto offset =
        static_cast<std::size_t>(mapped_prg_bank(addr)) * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper375::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if ((address_latch_ & kLockBit) == 0U) {
        address_latch_ = addr & kAddressLatchMask;
    }
    data_latch_ = value & 7U;
}

bool Mapper375::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper375::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper375::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper375::write_chr(Address addr, Byte value) {
    if ((address_latch_ & kOuterModeBit) == 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper375::mirror_mode() const {
    return (address_latch_ & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper375::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper375::reset() {
    address_latch_ = 0U;
    data_latch_ = 0U;
}

void Mapper375::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(address_latch_);
    writer.write(data_latch_);
    writer.write_array(chr_ram_);
}

void Mapper375::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto address_latch = reader.read<Address>();
    const auto data_latch = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        address_latch > kAddressLatchMask || data_latch > 7U) {
        reader.fail();
        return;
    }

    address_latch_ = address_latch;
    data_latch_ = data_latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
