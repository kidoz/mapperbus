#include "core/mappers/mapper435.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::uint16_t kStateTag = 0x4350U;
constexpr Address kLatchMask = 0x0F7FU;
constexpr Address kChrWriteProtect = 0x0800U;
constexpr Address kNromMode = 0x0200U;
constexpr Address kHorizontalMirror = 0x0002U;
constexpr Address kInnerA14 = 0x0001U;

} // namespace

// Original implementation from NES 2.0 mapper 435 oldid 19228. Soft reset retains
// CHR RAM; cold power clears it deterministically after restoring the zero latch.
Mapper435::Mapper435(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper435::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 435U) {
        return std::unexpected("Mapper 435 requires mapper ID 435");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 435 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 435 requires 2 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 435 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 435 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 435 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper435::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const bool upper = (addr & 0x4000U) != 0U;
    const unsigned outer = ((latch_ >> 7U) & 0x08U) | ((latch_ >> 6U) & 0x04U) |
                           ((latch_ >> 5U) & 0x02U) | ((latch_ >> 5U) & 0x01U);
    unsigned inner = (latch_ >> 2U) & 0x07U;
    if ((latch_ & kNromMode) == 0U) {
        if (upper) {
            inner = 7U;
        }
    } else if ((latch_ & kInnerA14) == 0U) {
        inner = (inner & 0x06U) | static_cast<unsigned>(upper);
    }

    const auto bank = (outer << 3U) | inner;
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper435::write_prg(Address addr, Byte) {
    if (addr >= 0x8000U) {
        latch_ = addr & kLatchMask;
    }
}

bool Mapper435::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper435::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper435::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper435::write_chr(Address addr, Byte value) {
    if ((latch_ & kChrWriteProtect) == 0U) {
        chr_ram_[addr & (kChrRamSize - 1U)] = value;
    }
}

MirrorMode Mapper435::mirror_mode() const {
    return (latch_ & kHorizontalMirror) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper435::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper435::reset() {
    latch_ = 0U;
}

void Mapper435::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Mapper435::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Address>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        (latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }

    latch_ = latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
