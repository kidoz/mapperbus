#include "core/mappers/g0151.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
// Original implementation from NESdev mapper 246 r22048 and the board/GAL
// tracing report (thread 13969). Prefer the measured narrow decode and vector
// override to older register-only descriptions. No analog floating-pin model.
G0151::G0151(const INesHeader& h, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_(h.mirror_mode) {
    power_on();
}
Result<void> G0151::validate_geometry(const INesHeader& h,
                                      std::size_t prg_size,
                                      std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 246 supports submapper 0");
    if (prg_size != 524288U || chr_size != 524288U)
        return std::unexpected("Mapper 246 requires 512 KiB PRG and 512 KiB CHR ROM");
    if (!h.has_battery || h.chr_ram_size != 0U || h.chr_nvram_size != 0U ||
        (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 2048U)))
        return std::unexpected("Mapper 246 requires 2 KiB battery RAM and no CHR RAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 246 requires fixed H/V mirroring");
    return {};
}
Byte G0151::read_prg(Address addr) {
    if (addr >= 0x8000U) {
        unsigned bank = prg_banks_[(addr >> 13U) & 3U];
        // These reads force A17 high without changing the stored bank or A18.
        if ((addr & 0xFFE4U) == 0xFFE4U)
            bank |= 0x10U;
        return prg_rom_[bank * 8192U + (addr & 0x1FFFU)];
    }
    return addr >= 0x6800U && addr <= 0x6FFFU ? wram_[addr & 0x7FFU] : 0U;
}
void G0151::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr <= 0x601FU) {
        if ((addr & 4U) == 0U)
            prg_banks_[addr & 3U] = value & 0x3FU;
        else
            chr_banks_[addr & 3U] = value;
    } else if (addr >= 0x6800U && addr <= 0x6FFFU) {
        wram_[addr & 0x7FFU] = value;
    }
}
bool G0151::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6800U && addr <= 0x6FFFU);
}
Byte G0151::read_chr(Address addr) {
    return chr_rom_[static_cast<unsigned>(chr_banks_[(addr >> 11U) & 3U]) * 2048U +
                    (addr & 0x7FFU)];
}
void G0151::write_chr(Address, Byte) {}
MirrorMode G0151::mirror_mode() const {
    return mirror_;
}
void G0151::reset() {
    // No reset input to the discrete register files: console reset retains them.
}
void G0151::power_on() {
    // R3=$FF is documented. All-high for the other 74'670 cells is the
    // deterministic published-reference policy, not a guarantee for every chip.
    prg_banks_.fill(0x3FU);
    chr_banks_.fill(0xFFU);
}
std::span<const Byte> G0151::battery_ram() const {
    return wram_;
}
void G0151::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), wram_.size()), wram_.begin());
}
uint16_t G0151::state_tag() const {
    return static_cast<uint16_t>(0xF600U | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}
void G0151::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write_array(wram_);
}
void G0151::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<Byte, 4> prg{}, chr{};
    std::array<Byte, 2048> wram{};
    reader.read_array(prg);
    reader.read_array(chr);
    reader.read_array(wram);
    if (!reader.ok() || tag != state_tag() ||
        !std::all_of(prg.begin(), prg.end(), [](Byte bank) { return bank <= 63U; })) {
        reader.fail();
        return;
    }
    prg_banks_ = prg;
    chr_banks_ = chr;
    wram_ = wram;
}
} // namespace mapperbus::core
