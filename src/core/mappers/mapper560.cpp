#include "core/mappers/mapper560.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
Mapper560::Mapper560(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : submapper_(h.submapper), prg_(std::move(prg)), chr_(std::move(chr)) {}
Result<void> Mapper560::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 560 || !h.is_nes2 || h.submapper > 1 || h.has_battery || h.has_trainer ||
        prg != 65536 || chr != 131072 || h.prg_ram_size != 8192 || h.prg_nvram_size ||
        h.chr_nvram_size || h.chr_ram_size || h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("C/E BASIC requires 64/128 KiB PRG/CHR ROM and 8 KiB work RAM");
    return {};
}
bool Mapper560::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}
Byte Mapper560::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return prg_[(mode_ ? 32768U : 0U) + (addr & 0x7FFFU)];
    return addr >= 0x6000U ? wram_[addr & 8191U] : 0U;
}
void Mapper560::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        mode_ = !mode_;
    else if (addr >= 0x6000U)
        wram_[addr & 8191U] = value;
}
Byte Mapper560::pattern_byte(Address addr, Byte shadow) const {
    addr &= 0xFFFU;
    if (submapper_ == 0) {
        const unsigned base = mode_ ? ((shadow & 63U) << 11U) : ((addr & 8U) << 13U);
        return chr_[base | ((addr & 0xFF0U) >> 1U) | (addr & 7U)];
    }
    if (!mode_ || !(shadow & 0xC0U))
        return chr_[addr];
    // Shadow D6/D7 enable the high/low bitplane of the monochrome glyph.
    if (!(shadow & ((addr & 8U) ? 0x40U : 0x80U)))
        return 0;
    return chr_[((shadow & 31U) << 12U) | (addr & 0xFF7U) | ((shadow & 32U) >> 2U)];
}
Byte Mapper560::read_chr(Address addr) {
    return pattern_byte(addr, shadow_[tile_]);
}
void Mapper560::write_chr(Address, Byte) {}
MirrorMode Mapper560::mirror_mode() const {
    return MirrorMode::Vertical;
}
bool Mapper560::write_nametable(Address addr, Byte value) {
    if (addr & 0x400U)
        shadow_[addr & 0x3FFU] = value;
    return false; // The same write also reaches console CIRAM.
}
bool Mapper560::observes_ppu_reads() const {
    return true;
}
void Mapper560::observe_ppu_read(Address addr) {
    const bool nt = (addr & 0x2000U) != 0;
    if (nt && !previous_nt_)
        tile_ = addr & 0x3FFU;
    previous_nt_ = nt;
}
std::optional<std::array<Byte, 2>> Mapper560::background_pattern(Address tile,
                                                                 Address pattern) const {
    const Byte shadow = shadow_[tile & 0x3FFU];
    return std::array{pattern_byte(pattern, shadow),
                      pattern_byte(static_cast<Address>(pattern + 8U), shadow)};
}
void Mapper560::reset() {
    mode_ = true;
    previous_nt_ = false;
    tile_ = 0;
}
void Mapper560::save_state(StateWriter& w) const {
    w.write<Word>(560);
    w.write(submapper_);
    w.write<Byte>(mode_);
    w.write<Byte>(previous_nt_);
    w.write(tile_);
    w.write_array(shadow_);
    w.write_array(wram_);
}
void Mapper560::load_state(StateReader& r) {
    auto tag = r.read<Word>();
    auto sub = r.read<Byte>();
    auto mode = r.read<Byte>();
    auto nt = r.read<Byte>();
    auto tile = r.read<Word>();
    decltype(shadow_) shadow{};
    decltype(wram_) wram{};
    r.read_array(shadow);
    r.read_array(wram);
    if (!r.ok() || tag != 560 || sub != submapper_ || mode > 1 || nt > 1 || tile >= 1024) {
        r.fail();
        return;
    }
    mode_ = mode != 0;
    previous_nt_ = nt != 0;
    tile_ = tile;
    shadow_ = shadow;
    wram_ = wram;
}
} // namespace mapperbus::core
