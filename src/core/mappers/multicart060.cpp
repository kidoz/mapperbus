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
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode),
      alternating_mirror_(header.mapper_number == 352U && header.submapper == 0U),
      prg_page_size_(header.mapper_number == 352U ? 0x8000U : 0x4000U) {}

Result<void> Multicart060::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.mapper_number == 352U) {
        // Each reset clocks the same game number onto both ROM chips.
        if (!header.is_nes2 || header.submapper > 1U || prg_size != 0x20000U || chr_size != 0x8000U)
            return std::unexpected(
                "Mapper 352 requires four paired 32 KiB PRG and 8 KiB CHR games");
        if (header.has_trainer || header.has_battery || header.prg_ram_size ||
            header.prg_nvram_size || header.chr_ram_size || header.chr_nvram_size ||
            (header.mirror_mode != MirrorMode::Horizontal &&
             header.mirror_mode != MirrorMode::Vertical))
            return std::unexpected(
                "Mapper 352 requires a ROM-only board with ordinary CIRAM metadata");
        return {};
    }
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
    return prg_rom_[static_cast<std::size_t>(game_) * prg_page_size_ +
                    (addr & (prg_page_size_ - 1U))];
}

void Multicart060::write_prg(Address, Byte) {}

Byte Multicart060::read_chr(Address addr) {
    return chr_rom_[static_cast<std::size_t>(game_) * 0x2000U + (addr & 0x1FFFU)];
}

void Multicart060::write_chr(Address, Byte) {}

MirrorMode Multicart060::mirror_mode() const {
    if (alternating_mirror_)
        return (game_ & 1U) ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return mirror_mode_;
}

void Multicart060::power_on() {
    game_ = 0U;
}

void Multicart060::reset() {
    game_ = static_cast<std::uint16_t>((game_ + 1U) % (prg_rom_.size() / prg_page_size_));
}

void Multicart060::save_state(StateWriter& writer) const {
    if (prg_page_size_ == 0x8000U) {
        writer.write<std::uint16_t>(alternating_mirror_ ? 0x3520U : 0x3521U);
        writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
        writer.write(mirror_mode_);
        writer.write(game_);
    } else {
        writer.write(kStateTag);
        writer.write(static_cast<Byte>(game_));
    }
}

void Multicart060::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    if (prg_page_size_ == 0x8000U) {
        const auto size = reader.read<std::uint32_t>();
        const auto mirror = reader.read<MirrorMode>();
        const auto game = reader.read<std::uint16_t>();
        if (!reader.ok() || tag != (alternating_mirror_ ? 0x3520U : 0x3521U) ||
            size != prg_rom_.size() || mirror != mirror_mode_ ||
            game >= prg_rom_.size() / prg_page_size_) {
            reader.fail();
            return;
        }
        game_ = game;
        return;
    }
    const auto game = reader.read<Byte>();
    if (!reader.ok() || tag != kStateTag || game > 3U) {
        reader.fail();
        return;
    }
    game_ = game;
}

} // namespace mapperbus::core
