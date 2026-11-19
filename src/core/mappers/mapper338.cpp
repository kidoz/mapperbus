#include "core/mappers/mapper338.hpp"

#include <bit>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 16384UZ;
constexpr std::size_t kChrBankSize = 8192UZ;
constexpr std::size_t kMaxPrgSize = 8UZ * kPrgBankSize;
constexpr std::size_t kMaxChrSize = 8UZ * kChrBankSize;
constexpr std::uint16_t kStateTag = 0x3380U;

} // namespace

// Board decoding follows NESdev mapper 338 r7867. The article does not specify
// reset values; bank zero and horizontal reset mirroring are deterministic policies.
Mapper338::Mapper338(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper338::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 338U) {
        return std::unexpected("Mapper 338 requires mapper ID 338");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 338 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kPrgBankSize || prg_size > kMaxPrgSize) {
        return std::unexpected("Mapper 338 requires power-of-two 16-128 KiB PRG ROM");
    }
    if (!std::has_single_bit(chr_size) || chr_size < kChrBankSize || chr_size > kMaxChrSize) {
        return std::unexpected("Mapper 338 requires power-of-two 8-64 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 338 requires no RAM, NVRAM, or battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 338 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper338::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const std::size_t bank = latch_ & 7U;
    const std::size_t offset = bank * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper338::write_prg(Address addr, Byte) {
    if (addr >= 0x8000U) {
        latch_ = addr & 0x0FU;
    }
}

Byte Mapper338::read_chr(Address addr) {
    const std::size_t bank = latch_ & 7U;
    const std::size_t offset = bank * kChrBankSize + (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper338::write_chr(Address, Byte) {}

MirrorMode Mapper338::mirror_mode() const {
    return (latch_ & 0x08U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

void Mapper338::reset() {
    latch_ = 0U;
}

void Mapper338::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(latch_);
}

void Mapper338::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || latch > 0x0FU) {
        reader.fail();
        return;
    }

    latch_ = latch;
}

} // namespace mapperbus::core
