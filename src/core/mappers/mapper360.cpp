#include "core/mappers/mapper360.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kOuterBankSize = 0x4000UZ;
constexpr std::uint16_t kStateTag = 0x3600U;

} // namespace

// NESdev mapper 360 oldid 7948 defines only the five DIP inputs, outer PRG
// banking and CIRAM selection. CHR RAM clearing on cold power and retention on
// soft reset are deterministic policies; electrical startup is not established.
Mapper360::Mapper360(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper360::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 360U) {
        return std::unexpected("Mapper 360 requires mapper ID 360");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 360 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize) {
        return std::unexpected("Mapper 360 requires exactly 512 KiB PRG ROM");
    }
    if (chr_size != 0U) {
        return std::unexpected("Mapper 360 requires no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 360 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 360 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper360::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto offset =
        dip_switches_ == 0U
            ? static_cast<std::size_t>(addr & 0x7FFFU)
            : static_cast<std::size_t>(dip_switches_) * kOuterBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset];
}

void Mapper360::write_prg(Address, Byte) {}

bool Mapper360::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper360::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper360::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper360::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper360::mirror_mode() const {
    return (dip_switches_ & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

Result<void> Mapper360::configure_board_jumpers(Byte value) {
    if (value > 31U) {
        return std::unexpected("Mapper 360 DIP switches accept values 0 through 31");
    }
    dip_switches_ = value;
    return {};
}

void Mapper360::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper360::reset() {}

std::uint16_t Mapper360::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | dip_switches_);
}

void Mapper360::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write_array(chr_ram_);
}

void Mapper360::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U) {
        reader.fail();
        return;
    }
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
