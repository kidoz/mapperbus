#include "core/mappers/mapper382.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kSmallPrgSize = 0x080000UZ;
constexpr std::size_t kLargePrgSize = 0x100000UZ;
constexpr std::size_t kOuterBankSize = 0x020000UZ;
constexpr std::size_t kBnromBankSize = 0x008000UZ;
constexpr std::size_t kUnromBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x3820U;
constexpr Byte kOuterBankMask = 0x3FU;
constexpr Byte kOuterLock = 0x20U;
constexpr Byte kHorizontalMirroring = 0x10U;
constexpr Byte kBnromMode = 0x08U;
constexpr Byte kInnerBankMask = 0x07U;

} // namespace

// Original implementation from NESdev mapper 382 oldid 24536. Lock retains the
// complete outer register until reset. Soft reset applies the documented zero
// power-on outer value and a deterministic zero inner latch while retaining CHR.
Mapper382::Mapper382(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper382::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 382U) {
        return std::unexpected("Mapper 382 requires mapper ID 382");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 382 requires NES 2.0 submapper 0");
    }
    if ((prg_size != kSmallPrgSize && prg_size != kLargePrgSize) || chr_size != 0U) {
        return std::unexpected("Mapper 382 requires 512 KiB or 1 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 382 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 382 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper382::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const auto outer = static_cast<std::size_t>(outer_bank_ & 0x07U) * kOuterBankSize;
    std::size_t offset = 0U;
    if ((outer_bank_ & kBnromMode) != 0U) {
        offset = outer + static_cast<std::size_t>(inner_bank_ & 0x03U) * kBnromBankSize +
                 (addr & 0x7FFFU);
    } else {
        const Byte bank = addr < 0xC000U ? inner_bank_ : 0x07U;
        offset = outer + static_cast<std::size_t>(bank) * kUnromBankSize + (addr & 0x3FFFU);
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper382::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    const Byte latched = value & read_prg(addr);
    if ((outer_bank_ & kOuterLock) == 0U) {
        outer_bank_ = static_cast<Byte>(addr) & kOuterBankMask;
    }
    inner_bank_ = latched & kInnerBankMask;
}

bool Mapper382::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper382::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper382::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper382::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper382::mirror_mode() const {
    return (outer_bank_ & kHorizontalMirroring) != 0U ? MirrorMode::Horizontal
                                                      : MirrorMode::Vertical;
}

void Mapper382::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper382::reset() {
    outer_bank_ = 0U;
    inner_bank_ = 0U;
}

void Mapper382::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(outer_bank_);
    writer.write(inner_bank_);
    writer.write_array(chr_ram_);
}

void Mapper382::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer_bank = reader.read<Byte>();
    const auto inner_bank = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        (outer_bank & ~kOuterBankMask) != 0U || (inner_bank & ~kInnerBankMask) != 0U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer_bank;
    inner_bank_ = inner_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
