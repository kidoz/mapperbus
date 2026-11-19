#include "core/mappers/mapper285.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {

Mapper285::Mapper285(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), submapper_(header.is_nes2 ? header.submapper : 0U) {}

Result<void> Mapper285::validate_geometry(const INesHeader& h,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (h.mapper_number != 285U)
        return std::unexpected("Mapper285 requires mapper ID 285");
    if (h.is_nes2 && h.submapper > 1U)
        return std::unexpected("Mapper 285 supports submappers 0 and 1");
    if (prg_size < 0x4000U || prg_size > 0x80000U || prg_size % 0x4000U != 0U || chr_size != 0U)
        return std::unexpected("Mapper 285 requires 16 KiB-512 KiB PRG ROM and no CHR ROM");
    if (h.has_battery || (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U ||
                                        h.chr_ram_size != 8192U || h.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 285 requires 8 KiB volatile CHR RAM and no CPU RAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 285 supports ordinary H/V CIRAM metadata");
    return {};
}

Byte Mapper285::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    std::size_t offset;
    if ((latch_ & 0x40U) != 0U) {
        offset = static_cast<std::size_t>(latch_ & 7U) * 0x8000U + (addr & 0x7FFFU);
    } else {
        const unsigned outer = submapper_ == 0U ? (latch_ >> 3U) & 3U : (latch_ >> 4U) & 3U;
        const unsigned inner = addr < 0xC000U ? latch_ & 7U : 7U;
        offset = (outer * 8U + inner) * 0x4000U + (addr & 0x3FFFU);
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper285::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        latch_ = value;
}

Byte Mapper285::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper285::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper285::mirror_mode() const {
    const Byte select = submapper_ == 0U || (latch_ & 0x80U) != 0U ? latch_ & 0x20U : latch_ & 8U;
    if ((latch_ & 0x80U) != 0U)
        return select != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    return select != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper285::power_on() {
    latch_ = 0U;
    chr_ram_.fill(0U);
}

void Mapper285::reset() {
    latch_ = 0U;
}

uint16_t Mapper285::state_tag() const {
    return static_cast<uint16_t>(0xF500U | submapper_);
}

void Mapper285::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write<uint32_t>(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Mapper285::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto latch = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size()) {
        reader.fail();
        return;
    }
    latch_ = latch;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
