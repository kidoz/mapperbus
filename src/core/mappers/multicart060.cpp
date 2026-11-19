#include "core/mappers/multicart060.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::uint16_t kStateTag = 0x3C00U;
} // namespace

Multicart060::Multicart060(const INesHeader& header,
                           std::vector<Byte> prg_rom,
                           std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode) {
}

Result<void> Multicart060::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 060 supports submapper 0");
    if (prg_size != 0x10000U || chr_size != 0x8000U)
        return std::unexpected("Mapper 060 requires 64 KiB PRG ROM and 32 KiB CHR ROM");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 060 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 060 supports fixed H/V CIRAM mirroring");
    return {};
}

Byte Multicart060::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    return prg_rom_[static_cast<std::size_t>(game_) * 0x4000U + (addr & 0x3FFFU)];
}

void Multicart060::write_prg(Address, Byte) {}

Byte Multicart060::read_chr(Address addr) {
    return chr_rom_[static_cast<std::size_t>(game_) * 0x2000U + (addr & 0x1FFFU)];
}

void Multicart060::write_chr(Address, Byte) {}

MirrorMode Multicart060::mirror_mode() const {
    return mirror_mode_;
}

void Multicart060::power_on() {
    game_ = 0U;
}

void Multicart060::reset() {
    game_ = (game_ + 1U) & 3U;
}

void Multicart060::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(game_);
}

void Multicart060::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto game = reader.read<Byte>();
    if (!reader.ok() || tag != kStateTag || game > 3U) {
        reader.fail();
        return;
    }
    game_ = game;
}

} // namespace mapperbus::core
