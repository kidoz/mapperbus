#include "core/mappers/mapper415.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x020000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kFixedPrgSize = 0x008000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4150U;
constexpr Byte kDataLatchMask = 0x1FU;
constexpr Byte kHorizontalMirror = 0x10U;

} // namespace

// Original implementation from NESdev mapper 415 oldid 8043. Electrical reset
// is unspecified: a cleared latch is deterministic, soft reset retains CHR RAM,
// and cold power clears it.
Mapper415::Mapper415(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper415::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 415U) {
        return std::unexpected("Mapper 415 requires mapper ID 415");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 415 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size != 0U) {
        return std::unexpected("Mapper 415 requires 32-128 KiB PRG in 8 KiB units and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 415 requires exactly 8 KiB volatile CHR RAM and no CPU RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 415 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 415 does not provide trainer RAM");
    }
    return {};
}

bool Mapper415::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper415::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper415::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_.empty()) {
        return 0U;
    }
    if (addr >= 0x8000U) {
        return prg_rom_[prg_rom_.size() - kFixedPrgSize + (addr & (kFixedPrgSize - 1U))];
    }
    const auto offset =
        static_cast<std::size_t>(data_latch_ & 0x0FU) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper415::write_prg(Address addr, Byte value) {
    if ((addr & 0x8000U) != 0U) {
        data_latch_ = value & kDataLatchMask;
    }
}

Byte Mapper415::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper415::write_chr(Address addr, Byte value) {
    chr_ram_[addr & (kChrRamSize - 1U)] = value;
}

MirrorMode Mapper415::mirror_mode() const {
    return (data_latch_ & kHorizontalMirror) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper415::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper415::reset() {
    data_latch_ = 0U;
}

void Mapper415::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write<std::uint32_t>(kChrRamSize);
    writer.write(data_latch_);
    writer.write_array(chr_ram_);
}

void Mapper415::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto ram_size = reader.read<std::uint32_t>();
    const auto data_latch = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        ram_size != kChrRamSize || (data_latch & ~kDataLatchMask) != 0U) {
        reader.fail();
        return;
    }

    data_latch_ = data_latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
