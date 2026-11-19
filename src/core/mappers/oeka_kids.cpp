#include "core/mappers/oeka_kids.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0x6000U;
} // namespace

OekaKids::OekaKids(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> OekaKids::validate_geometry(const INesHeader& header,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 096 supports submapper 0");
    if (prg_size != 0x20000U || chr_size != 0U)
        return std::unexpected("Mapper 096 requires 128 KiB PRG ROM and no CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.chr_ram_size != 0x8000U)))
        return std::unexpected("Mapper 096 requires 32 KiB volatile CHR RAM and no other RAM");
    if (header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 096 requires hardwired vertical mirroring");
    return {};
}

Byte OekaKids::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const std::size_t offset =
        static_cast<std::size_t>(outer_bank_ & 3U) * 0x8000U + (addr & 0x7FFFU);
    return prg_rom_[offset];
}

void OekaKids::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    outer_bank_ = static_cast<Byte>(value & read_prg(addr) & 7U);
}

std::size_t OekaKids::chr_offset(Address addr) const {
    const std::size_t bank =
        static_cast<std::size_t>(outer_bank_ & 4U) + (addr < 0x1000U ? inner_bank_ : 3U);
    return bank * 0x1000U + (addr & 0x0FFFU);
}

Byte OekaKids::read_chr(Address addr) {
    return chr_ram_[chr_offset(addr)];
}

void OekaKids::write_chr(Address addr, Byte value) {
    chr_ram_[chr_offset(addr)] = value;
}

MirrorMode OekaKids::mirror_mode() const {
    return MirrorMode::Vertical;
}

bool OekaKids::observes_ppu_address() const {
    return true;
}

void OekaKids::observe_ppu_address(Address addr) {
    const auto region = static_cast<Byte>((addr >> 12U) & 3U);
    if (region == 2U && ppu_region_ != 2U)
        inner_bank_ = static_cast<Byte>((addr >> 8U) & 3U);
    ppu_region_ = region;
}

void OekaKids::reset() {
    // The discrete latches have no console-reset input; retain RAM and pin phase.
}

void OekaKids::power_on() {
    outer_bank_ = 0U;
    inner_bank_ = 0U;
    ppu_region_ = 0U;
    chr_ram_.fill(0U);
}

void OekaKids::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(outer_bank_);
    writer.write(inner_bank_);
    writer.write(ppu_region_);
    writer.write_array(chr_ram_);
}

void OekaKids::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto inner = reader.read<Byte>();
    const auto region = reader.read<Byte>();
    std::array<Byte, 0x8000> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != kStateTag || outer > 7U || inner > 3U || region > 3U) {
        reader.fail();
        return;
    }
    outer_bank_ = outer;
    inner_bank_ = inner;
    ppu_region_ = region;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
