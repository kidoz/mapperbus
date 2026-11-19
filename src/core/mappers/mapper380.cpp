#include "core/mappers/mapper380.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kMinPrgSize = 0x040000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x8000U;
constexpr Address kLatchMask = 0x03FFU;
constexpr Address kOuterMode = 0x0200U;
constexpr Address kMenuMode = 0x0100U;
constexpr Address kChrWriteProtect = 0x0080U;
constexpr Address kNrom128Mode = 0x0001U;

} // namespace

// Original implementation from NESdev mapper 380 oldid 18847. Reset clears the
// documented latch and retains CHR RAM and solder pads; cold power also clears
// volatile CHR RAM deterministically.
Mapper380::Mapper380(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), submapper_(header.submapper) {
    power_on();
}

Result<void> Mapper380::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 380U) {
        return std::unexpected("Mapper 380 requires mapper ID 380");
    }
    if (!header.is_nes2 || header.submapper > 1U) {
        return std::unexpected("Mapper 380 requires NES 2.0 submapper 0 or 1");
    }
    if ((prg_size != kMinPrgSize && prg_size != kMaxPrgSize) || chr_size != 0U) {
        return std::unexpected("Mapper 380 requires 256 or 512 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 380 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 380 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper380::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const bool upper = (addr & 0x4000U) != 0U;
    unsigned bank = (latch_ >> 2U) & 0x1FU;
    if ((latch_ & kOuterMode) == 0U) {
        if (upper) {
            bank = (bank & 0x18U) | 7U;
        }
    } else if (!upper && (latch_ & kNrom128Mode) == 0U) {
        bank &= 0x1EU;
    }
    if (submapper_ == 1U && upper && (latch_ & kMenuMode) != 0U) {
        bank |= 0x08U;
    }

    auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    if (submapper_ == 0U && (latch_ & kMenuMode) != 0U) {
        offset = (offset & ~0x0FU) | pads_;
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper380::write_prg(Address addr, Byte) {
    if (addr >= 0x8000U) {
        latch_ = addr & kLatchMask;
    }
}

bool Mapper380::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper380::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper380::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper380::write_chr(Address addr, Byte value) {
    if ((latch_ & kChrWriteProtect) == 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper380::mirror_mode() const {
    return (latch_ & 0x0002U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

Result<void> Mapper380::configure_board_jumpers(Byte value) {
    if (submapper_ != 0U || value > 15U) {
        return std::unexpected("Mapper 380 solder pads require submapper 0 and a value 0..15");
    }
    pads_ = value;
    return {};
}

void Mapper380::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper380::reset() {
    latch_ = 0U;
}

std::uint16_t Mapper380::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | (submapper_ << 4U) | pads_);
}

void Mapper380::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Mapper380::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Address>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        (latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }

    latch_ = latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
