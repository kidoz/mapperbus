#include "core/mappers/single_chip.hpp"

#include <bit>

#include "core/state/state.hpp"

namespace mapperbus::core {

// Original address wiring from NESdev mapper 218 r22660. CIRAM stays owned
// and serialized by the console PPU; the cartridge contains no RAM or latches.
SingleChip::SingleChip(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), wiring_(header.nametable_bits),
      ciram_pin_((wiring_ & 8U) != 0U ? 12U + (wiring_ & 1U) : 11U - (wiring_ & 1U)) {}

Result<void> SingleChip::validate_geometry(const INesHeader& header,
                                           std::size_t prg_size,
                                           std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 218 supports submapper 0");
    if (!std::has_single_bit(prg_size) || prg_size > 0x8000U || chr_size != 0U)
        return std::unexpected(
            "Mapper 218 requires power-of-two PRG ROM up to 32 KiB and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 0U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 218 has no cartridge RAM or battery");
    const auto expected_mirror = (header.nametable_bits & 8U) != 0U   ? MirrorMode::FourScreen
                                 : (header.nametable_bits & 1U) != 0U ? MirrorMode::Vertical
                                                                      : MirrorMode::Horizontal;
    if ((header.nametable_bits & ~9U) != 0U || header.mirror_mode != expected_mirror)
        return std::unexpected("Mapper 218 requires consistent raw nametable wiring flags");
    return {};
}

Byte SingleChip::read_prg(Address addr) {
    return addr >= 0x8000U ? prg_rom_[(addr - 0x8000U) & (prg_rom_.size() - 1U)] : 0U;
}

std::optional<Address> SingleChip::ciram_address(Address addr) const {
    if (addr >= 0x3F00U)
        return std::nullopt;
    return static_cast<Address>((addr & 0x3FFU) | (((addr >> ciram_pin_) & 1U) << 10U));
}

MirrorMode SingleChip::mirror_mode() const {
    if ((wiring_ & 8U) != 0U)
        return wiring_ == 8U ? MirrorMode::SingleLower : MirrorMode::SingleUpper;
    return wiring_ == 1U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

void SingleChip::save_state(StateWriter& writer) const {
    writer.write<uint16_t>(static_cast<uint16_t>(0xDA00U | wiring_));
}

void SingleChip::load_state(StateReader& reader) {
    if (reader.read<uint16_t>() != (0xDA00U | wiring_))
        reader.fail();
}

} // namespace mapperbus::core
