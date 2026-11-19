#include "core/mappers/multicart063.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev mapper 063 r19134 and dumping threads 19197/16368. C shares the
// highest PRG bank pin: eight bank bits on submapper 0 and seven on submapper 1.
Multicart063::Multicart063(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), submapper_(header.is_nes2 ? header.submapper : 0U) {}

Result<void> Multicart063::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    const Byte submapper = header.is_nes2 ? header.submapper : 0U;
    if (submapper > 1U)
        return std::unexpected("Mapper 063 supports submappers 0 and 1");
    const std::size_t max_prg_size = submapper == 0U ? 0x400000U : 0x200000U;
    if (prg_size == 0U || prg_size > max_prg_size || prg_size % 0x4000U != 0U || chr_size != 0U)
        return std::unexpected("Mapper 063 requires 16 KiB-aligned PRG ROM within the bank pins "
                               "and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 063 requires 8 KiB volatile CHR RAM and no CPU RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 063 supports ordinary H/V CIRAM metadata");
    return {};
}

Address Multicart063::latch_mask() const {
    return submapper_ == 0U ? 0x07FFU : 0x03FFU;
}

unsigned Multicart063::prg_bank(Address addr) const {
    unsigned bank = (latch_ >> 3U) & (submapper_ == 0U ? 0xFFU : 0x7FU);
    const unsigned low = (latch_ & 0x02U) != 0U ? (addr >> 14U) & 1U : (latch_ >> 2U) & 1U;
    bank = (bank & ~1U) | low;
    if (submapper_ == 1U)
        bank %= prg_rom_.size() / 0x4000U;
    return bank;
}

bool Multicart063::maps_prg(Address addr) const {
    return addr >= 0x8000U && (submapper_ == 1U || prg_bank(addr) < prg_rom_.size() / 0x4000U);
}

Byte Multicart063::read_prg(Address addr) {
    if (!maps_prg(addr))
        return 0U;
    return prg_rom_[prg_bank(addr) * 0x4000U + (addr & 0x3FFFU)];
}

void Multicart063::write_prg(Address addr, Byte /*value*/) {
    if (addr >= 0x8000U)
        latch_ = addr & latch_mask();
}

Byte Multicart063::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Multicart063::write_chr(Address addr, Byte value) {
    const Address protect = submapper_ == 0U ? 0x0400U : 0x0200U;
    if ((latch_ & protect) == 0U)
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Multicart063::mirror_mode() const {
    return (latch_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Multicart063::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Multicart063::reset() {
    latch_ = 0U;
}

std::uint32_t Multicart063::state_tag() const {
    return 0x06300000U | (static_cast<std::uint32_t>(submapper_) << 16U) |
           static_cast<std::uint32_t>(prg_rom_.size() / 0x4000U);
}

void Multicart063::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Multicart063::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint32_t>();
    const auto latch = reader.read<Address>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || (latch & ~latch_mask()) != 0U) {
        reader.fail();
        return;
    }
    latch_ = latch;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
