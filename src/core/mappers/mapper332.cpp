#include "core/mappers/mapper332.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kSmallPrgSize = 128UZ * 1024UZ;
constexpr std::size_t kLargePrgSize = 256UZ * 1024UZ;
constexpr std::size_t kSmallChrSize = 64UZ * 1024UZ;
constexpr std::size_t kLargeChrSize = 128UZ * 1024UZ;
constexpr std::uint16_t kStateTag = 0x3320U;
constexpr Byte kSolderMask = 0xC0U;
constexpr Byte kChrModeMask = 0x30U;

} // namespace

// Original implementation from NESdev mapper 332 revision 7853. The Cart
// Reader's WS-1001 dumping procedure corroborates bit 7 as the outer A17/A16
// pin despite that bit being displayed as unused in the article's diagram.
Mapper332::Mapper332(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper332::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 332U) {
        return std::unexpected("Mapper 332 requires mapper ID 332");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 332 requires NES 2.0 submapper 0");
    }
    const bool small_population = prg_size == kSmallPrgSize && chr_size == kSmallChrSize;
    const bool large_population = prg_size == kLargePrgSize && chr_size == kLargeChrSize;
    if (!small_population && !large_population) {
        return std::unexpected(
            "Mapper 332 requires 128 KiB PRG/64 KiB CHR or 256 KiB PRG/128 KiB CHR");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 332 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 332 requires ordinary H/V nametable metadata");
    }
    return {};
}

bool Mapper332::maps_prg(Address addr) const {
    return addr >= 0x8000U && (chr_solder_register_ & kSolderMask) == 0U;
}

Byte Mapper332::read_prg(Address addr) {
    if (!maps_prg(addr)) {
        return 0U;
    }

    const std::size_t outer = static_cast<std::size_t>(prg_mirror_register_ & 0x80U) << 10U;
    const std::size_t high = static_cast<std::size_t>(prg_mirror_register_ & 0x0EU) << 13U;
    const std::size_t a14 = (prg_mirror_register_ & 0x10U) != 0U
                                ? static_cast<std::size_t>(prg_mirror_register_ & 1U) << 14U
                                : static_cast<std::size_t>(addr & 0x4000U);
    const std::size_t offset = outer | high | a14 | (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper332::write_prg(Address addr, Byte value) {
    if ((addr & 0xE001U) == 0x6000U) {
        prg_mirror_register_ = value;
        return;
    }
    if ((addr & 0xE001U) == 0x6001U) {
        chr_solder_register_ = value;
        return;
    }
    if (addr >= 0x8000U) {
        cnrom_latch_ = value & 3U;
    }
}

Byte Mapper332::read_chr(Address addr) {
    Byte inner = cnrom_latch_;
    switch (chr_solder_register_ & kChrModeMask) {
    case 0x20U:
        inner = static_cast<Byte>((cnrom_latch_ & 1U) | ((chr_solder_register_ & 1U) << 1U));
        break;
    case 0x30U:
        inner = chr_solder_register_ & 3U;
        break;
    default:
        // MM=01 is unassigned by r7853; CNROM-256 is the deterministic fallback.
        break;
    }

    const std::size_t outer = static_cast<std::size_t>(prg_mirror_register_ & 0x80U) << 9U;
    const std::size_t a15 = static_cast<std::size_t>(chr_solder_register_ & 4U) << 13U;
    const std::size_t offset =
        outer | a15 | (static_cast<std::size_t>(inner) << 13U) | (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper332::write_chr(Address, Byte) {}

MirrorMode Mapper332::mirror_mode() const {
    return (prg_mirror_register_ & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper332::reset() {
    prg_mirror_register_ = 0U;
    chr_solder_register_ = 0U;
    cnrom_latch_ = 0U;
}

void Mapper332::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(prg_mirror_register_);
    writer.write(chr_solder_register_);
    writer.write(cnrom_latch_);
}

void Mapper332::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto prg_mirror_register = reader.read<Byte>();
    const auto chr_solder_register = reader.read<Byte>();
    const auto cnrom_latch = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || cnrom_latch > 3U) {
        reader.fail();
        return;
    }

    prg_mirror_register_ = prg_mirror_register;
    chr_solder_register_ = chr_solder_register;
    cnrom_latch_ = cnrom_latch;
}

} // namespace mapperbus::core
