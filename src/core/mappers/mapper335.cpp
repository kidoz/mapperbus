#include "core/mappers/mapper335.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 32768U;
constexpr std::size_t kMaxPrgSize = 256U * 1024U;
constexpr std::size_t kMinChrSize = 8192U;
constexpr std::size_t kMaxChrSize = 128U * 1024U;
constexpr std::uint16_t kStateTag = 0x3350U;

} // namespace

// Board decoding follows NESdev mapper 335 r24495. The article does not specify
// reset values, so zeroed latches are an explicit deterministic reset policy.
Mapper335::Mapper335(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper335::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 335U) {
        return std::unexpected("Mapper 335 requires mapper ID 335");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 335 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 335 requires power-of-two 32-256 KiB PRG and 8-128 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 335 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 335 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper335::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const std::size_t bank = (prg_register_ >> 1U) & 7U;
    const std::size_t a14 = (prg_register_ & 0x10U) != 0U
                                ? static_cast<std::size_t>(prg_register_ & 1U) << 14U
                                : static_cast<std::size_t>(addr & 0x4000U);
    const std::size_t offset = bank * 32768U | a14 | (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper335::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U || (prg_register_ & 0x80U) != 0U) {
        return;
    }
    if (addr >= 0xC000U) {
        prg_register_ = value;
    } else {
        chr_register_ = value;
    }
}

Byte Mapper335::read_chr(Address addr) {
    const std::size_t offset =
        static_cast<std::size_t>(chr_register_ & 0x0FU) * 8192U | (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper335::write_chr(Address, Byte) {}

MirrorMode Mapper335::mirror_mode() const {
    return (prg_register_ & 0x20U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

void Mapper335::reset() {
    prg_register_ = 0U;
    chr_register_ = 0U;
}

void Mapper335::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(prg_register_);
    writer.write(chr_register_);
}

void Mapper335::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto prg_register = reader.read<Byte>();
    const auto chr_register = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size()) {
        reader.fail();
        return;
    }

    prg_register_ = prg_register;
    chr_register_ = chr_register;
}

} // namespace mapperbus::core
