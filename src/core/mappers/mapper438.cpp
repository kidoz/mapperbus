#include "core/mappers/mapper438.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4380U;
constexpr Byte kLatchMask = 0x3FU;
constexpr Byte kNrom256Mode = 0x01U;
constexpr Byte kHorizontalMirror = 0x01U;

} // namespace

// Original implementation from NES 2.0 mapper 438 oldid 19303. Reset clearing is a
// deterministic compatibility policy; the board has no volatile cartridge RAM.
Mapper438::Mapper438(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper438::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 438U) {
        return std::unexpected("Mapper 438 requires mapper ID 438");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 438 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 438 requires 32-512 KiB PRG ROM in 16 KiB units");
    }
    if (chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected("Mapper 438 requires 8-256 KiB CHR ROM in 8 KiB units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 438 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 438 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 438 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper438::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    unsigned bank = registers_.address >> 1U;
    if ((registers_.address & kNrom256Mode) != 0U) {
        bank = (bank & ~1U) | ((addr >> 14U) & 1U);
    }
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper438::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        registers_.address = static_cast<Byte>(addr) & kLatchMask;
        registers_.data = value & kLatchMask;
    }
}

bool Mapper438::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper438::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper438::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const unsigned bank = registers_.data >> 1U;
    const auto offset = static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper438::write_chr(Address, Byte) {}

MirrorMode Mapper438::mirror_mode() const {
    return (registers_.data & kHorizontalMirror) != 0U ? MirrorMode::Horizontal
                                                       : MirrorMode::Vertical;
}

void Mapper438::reset() {
    registers_ = Registers{};
}

void Mapper438::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.address);
    writer.write(registers_.data);
}

void Mapper438::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.address);
    reader.read(next.data);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (next.address & ~kLatchMask) != 0U ||
        (next.data & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }

    registers_ = next;
}

} // namespace mapperbus::core
