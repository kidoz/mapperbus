#include "core/mappers/mapper378.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x7800U;
constexpr Byte kOuterBankMask = 0x3FU;
constexpr Byte kUnromMode = 0x20U;
constexpr Byte kMirrorSelect = 0x10U;

} // namespace

// Original implementation from NESdev mapper 378 oldid 7988. Soft reset clears
// the outer latch and retains CHR RAM; cold power additionally clears it. The
// article does not establish electrical reset or analog RAM initialization.
Mapper378::Mapper378(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper378::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 378U) {
        return std::unexpected("Mapper 378 requires mapper ID 378");
    }
    if (header.is_nes2 && header.submapper != 0U) {
        return std::unexpected("Mapper 378 supports only NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 378 requires exactly 512 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U))) {
        return std::unexpected("Mapper 378 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 378 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

unsigned Mapper378::mapped_prg_bank(Address addr) const {
    const bool upper = addr >= 0xC000U;
    const unsigned inner = outer_bank_ & 7U;
    if ((outer_bank_ & kUnromMode) == 0U) {
        return inner * 2U + static_cast<unsigned>(upper);
    }
    if (!upper) {
        return 16U + inner * 2U + ((outer_bank_ >> 3U) & 1U);
    }
    return 23U + ((outer_bank_ & 4U) << 1U);
}

Byte Mapper378::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto offset =
        static_cast<std::size_t>(mapped_prg_bank(addr)) * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper378::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    outer_bank_ = value & kOuterBankMask;
}

bool Mapper378::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper378::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper378::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper378::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper378::mirror_mode() const {
    const bool selected = (outer_bank_ & kMirrorSelect) != 0U;
    if ((outer_bank_ & kUnromMode) == 0U) {
        return selected ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
    return selected ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper378::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper378::reset() {
    outer_bank_ = 0U;
}

void Mapper378::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(outer_bank_);
    writer.write_array(chr_ram_);
}

void Mapper378::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer_bank = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        (outer_bank & ~kOuterBankMask) != 0U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
