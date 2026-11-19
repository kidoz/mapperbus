#include "core/mappers/mapper288.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 32U * 1024U;
constexpr std::size_t kChrBankSize = 8U * 1024U;
constexpr std::size_t kMaxPrgSize = 4U * kPrgBankSize;
constexpr std::size_t kMaxChrSize = 8U * kChrBankSize;
constexpr std::uint16_t kStateTag = 0x2880U;
constexpr Byte kLatchMask = 0x3FU;

} // namespace

// Original implementation from the immutable NESdev mapper 288 article, r24622.
Mapper288::Mapper288(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {}

Result<void> Mapper288::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 288U || !header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 288 requires NES 2.0 mapper 288 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kPrgBankSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kChrBankSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 288 requires power-of-two 32-128 KiB PRG and 8-64 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 288 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 288 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper288::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const std::size_t bank = (latch_ >> 3U) & 3U;
    const std::size_t offset = bank * kPrgBankSize + (addr & 0x7FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper288::write_prg(Address addr, Byte) {
    if (addr >= 0x8000U) {
        latch_ = static_cast<Byte>(addr) & kLatchMask;
    }
}

Byte Mapper288::read_chr(Address addr) {
    const std::size_t bank = latch_ & 7U;
    const std::size_t offset = bank * kChrBankSize + (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper288::write_chr(Address, Byte) {}

MirrorMode Mapper288::mirror_mode() const {
    return (latch_ & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper288::power_on() {
    reset();
}

void Mapper288::reset() {
    // Electrical reset is unspecified; zero is the deterministic compatibility policy.
    latch_ = 0U;
}

void Mapper288::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(latch_);
}

void Mapper288::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Byte>();
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }
    latch_ = latch;
}

} // namespace mapperbus::core
