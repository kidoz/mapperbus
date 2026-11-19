#include "core/mappers/mapper454.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::uint16_t kStateTag = 0x4540U;
constexpr Address kAddressLatchMask = 0x01FFU;
constexpr Address kUnromMode = 0x0100U;
constexpr Address kNromMode = 0x0080U;
constexpr Address kMirrorMask = 0x0002U;
constexpr Address kNrom256Mask = 0x0001U;
constexpr Byte kDataLatchMask = 0x07U;

} // namespace

// Original implementation from NESdev mapper 454 oldid 21017. Reset behavior is
// unspecified; clearing both latches is a deterministic compatibility policy.
Mapper454::Mapper454(const INesHeader& /*header*/,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper454::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 454U) {
        return std::unexpected("Mapper 454 requires mapper ID 454");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 454 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 454 requires exactly 1 MiB PRG ROM and no CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 454 has no CPU work RAM or battery-backed RAM");
    }
    if (header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 454 requires exactly 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 454 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 454 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper454::prg_bank(Address addr) const {
    const unsigned outer = ((address_latch_ & kUnromMode) >> 3U) | ((address_latch_ & 0x60U) >> 2U);
    const unsigned inner = (address_latch_ >> 2U) & 0x07U;
    const bool nrom256 = (address_latch_ & kNrom256Mask) != 0U;
    const bool upper = (addr & 0x4000U) != 0U;

    if (!upper) {
        unsigned selected = (address_latch_ & kUnromMode) != 0U ? data_latch_ : inner;
        if (nrom256) {
            selected &= ~1U;
        }
        return outer | selected;
    }
    if ((address_latch_ & kUnromMode) != 0U) {
        return outer | 0x07U;
    }
    if ((address_latch_ & kNromMode) == 0U) {
        return 0U;
    }
    return outer | inner | (nrom256 ? 1U : 0U);
}

std::size_t Mapper454::prg_offset(Address addr) const {
    return prg_bank(addr) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper454::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    return prg_rom_.empty() ? 0U : prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper454::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if ((address_latch_ & kUnromMode) != 0U) {
        data_latch_ = value & kDataLatchMask;
        return;
    }
    address_latch_ = addr & kAddressLatchMask;
}

bool Mapper454::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper454::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper454::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper454::write_chr(Address addr, Byte value) {
    chr_ram_[addr & (kChrRamSize - 1U)] = value;
}

MirrorMode Mapper454::mirror_mode() const {
    return (address_latch_ & kMirrorMask) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper454::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper454::reset() {
    address_latch_ = 0U;
    data_latch_ = 0U;
}

void Mapper454::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(kChrRamSize);
    writer.write(address_latch_);
    writer.write(data_latch_);
    writer.write_array(chr_ram_);
}

void Mapper454::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto address_latch = reader.read<Address>();
    const auto data_latch = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid = reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
                       chr_size == kChrRamSize && (address_latch & ~kAddressLatchMask) == 0U &&
                       (data_latch & ~kDataLatchMask) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    address_latch_ = address_latch;
    data_latch_ = data_latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
