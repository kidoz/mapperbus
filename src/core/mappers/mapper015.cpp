#include "core/mappers/mapper015.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0x0F00U;
} // namespace

Mapper015::Mapper015(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> Mapper015::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 015 supports submapper 0");
    if (prg_size < 0x4000U || prg_size > 0x100000U || prg_size % 0x4000U != 0U || chr_size != 0U)
        return std::unexpected("Mapper 015 requires 16 KiB-1 MiB PRG ROM and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 015 requires 8 KiB volatile CHR RAM and no CPU RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 015 supports ordinary H/V CIRAM metadata");
    return {};
}

Byte Mapper015::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const unsigned mode = latch_ >> 6U;
    unsigned bank = latch_ & 0x3FU;
    if (mode == 0U)
        bank = (bank & 0x3EU) | ((addr >> 14U) & 1U);
    else if (mode == 1U && addr >= 0xC000U)
        bank |= 7U;
    const std::size_t offset =
        mode == 2U ? bank * 0x4000U + ((latch_ & 0x80U) != 0U ? 0x2000U : 0U) + (addr & 0x1FFFU)
                   : bank * 0x4000U + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper015::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        latch_ = value;
}

Byte Mapper015::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper015::write_chr(Address addr, Byte value) {
    const unsigned mode = latch_ >> 6U;
    if (mode == 1U || mode == 2U)
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper015::mirror_mode() const {
    return (latch_ & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper015::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper015::reset() {
    latch_ = 0U;
}

uint16_t Mapper015::state_tag() const {
    return static_cast<uint16_t>(kStateTag | (prg_rom_.size() / 0x4000U - 1U));
}

void Mapper015::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Mapper015::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto latch = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag()) {
        reader.fail();
        return;
    }
    latch_ = latch;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
