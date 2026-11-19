#include "core/mappers/mapper449.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4490U;
constexpr Address kLatchMask = 0x03FFU;
constexpr Address kMenuMode = 0x0200U;
constexpr Address kNromMode = 0x0080U;
constexpr Address kNrom256Mode = 0x0001U;
constexpr Address kHorizontalMirroring = 0x0002U;
constexpr Byte kChrBankMask = 0x03U;

} // namespace

// Original implementation from NESdev mapper 449 oldid 19306. Reset clears both
// documented latches and retains CHR RAM and solder pads; cold power clears RAM.
Mapper449::Mapper449(const INesHeader& /*header*/,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper449::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 449U) {
        return std::unexpected("Mapper 449 requires mapper ID 449");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 449 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 449 requires exactly 1 MiB PRG ROM and no CHR ROM");
    }
    if (header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 449 requires exactly 32 KiB volatile CHR RAM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 449 has no CPU RAM, NVRAM or battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 449 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 449 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper449::prg_offset(Address addr) const {
    const bool upper = (addr & 0x4000U) != 0U;
    const unsigned outer = ((latch_ >> 5U) & 3U) | ((latch_ >> 6U) & 4U);
    unsigned bank = (outer << 3U) | ((latch_ >> 2U) & 7U);
    if ((latch_ & kNromMode) == 0U) {
        if (upper) {
            bank = (outer << 3U) | 7U;
        }
    } else if ((latch_ & kNrom256Mode) != 0U) {
        bank = (bank & ~1U) | (upper ? 1U : 0U);
    }

    auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    if ((latch_ & kMenuMode) != 0U) {
        offset = (offset & ~0x0FUZ) | pads_;
    }
    return offset;
}

Byte Mapper449::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper449::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        latch_ = addr & kLatchMask;
        chr_bank_ = value & kChrBankMask;
    }
}

bool Mapper449::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper449::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper449::chr_offset(Address addr) const {
    return static_cast<std::size_t>(chr_bank_) * kChrBankSize + (addr & 0x1FFFU);
}

Byte Mapper449::read_chr(Address addr) {
    return chr_ram_[chr_offset(addr)];
}

void Mapper449::write_chr(Address addr, Byte value) {
    chr_ram_[chr_offset(addr)] = value;
}

MirrorMode Mapper449::mirror_mode() const {
    return (latch_ & kHorizontalMirroring) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

Result<void> Mapper449::configure_board_jumpers(Byte value) {
    if (value > 15U) {
        return std::unexpected("Mapper 449 solder pads require a value 0..15");
    }
    pads_ = value;
    return {};
}

void Mapper449::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper449::reset() {
    latch_ = 0U;
    chr_bank_ = 0U;
}

std::uint16_t Mapper449::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | pads_);
}

void Mapper449::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(kChrRamSize);
    writer.write(latch_);
    writer.write(chr_bank_);
    writer.write_array(chr_ram_);
}

void Mapper449::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Address>();
    const auto chr_bank = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_size == kChrRamSize && (latch & ~kLatchMask) == 0U &&
                       (chr_bank & ~kChrBankMask) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    latch_ = latch;
    chr_bank_ = chr_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
