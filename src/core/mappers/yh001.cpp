#include "core/mappers/yh001.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
Yh001::Yh001(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : Mmc3(h, std::move(prg), std::move(chr)) {
    power_on();
}

Result<void> Yh001::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.submapper || prg != 524288U || chr != 524288U || h.has_battery || h.has_trainer ||
        (h.is_nes2 && h.prg_ram_size) || h.prg_nvram_size || h.chr_ram_size || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected("YH-001 requires 512 KiB each of PRG and CHR ROM");
    return {};
}

bool Yh001::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Yh001::prg_read_mask(Address addr) const {
    if (addr >= 0x8000U)
        return 0xFFU;
    return (addr & 0xE003U) == 0x6002U ? 7U : 0U;
}

Byte Yh001::read_prg(Address addr) {
    if (addr < 0x8000U)
        return (addr & 0xE003U) == 0x6002U ? pads_ : 0U;
    if (outer_[0] & 0x80U) {
        unsigned bank = outer_[0] & 15U;
        if (outer_[0] & 0x20U)
            bank = (bank & 14U) | ((addr >> 14U) & 1U);
        return physical_prg(bank * 16384U + (addr & 0x3FFFU));
    }
    const unsigned mask = (outer_[1] & 0x20U) ? 31U : 15U;
    const unsigned base = ((outer_[1] & 2U) << 3U) | ((outer_[1] & 0x10U) << 1U);
    return physical_prg((base | (prg_bank_pins(addr) & mask)) * 8192U + (addr & 0x1FFFU));
}

Byte Yh001::read_chr(Address addr) {
    const unsigned mask = (outer_[1] & 0x40U) ? 255U : 127U;
    const unsigned base = ((outer_[1] & 2U) << 6U) | ((outer_[1] & 0x10U) << 4U);
    return physical_chr((base | (chr_bank(addr) & mask)) * 1024U + (addr & 0x3FFU));
}

void Yh001::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        Mmc3::write_prg(addr, value);
    else if (addr >= 0x6000U && !(outer_[1] & 1U) && !wram_write_protected())
        outer_[addr & 3U] = value;
}

Result<void> Yh001::configure_board_jumpers(Byte value) {
    if (value > 7U)
        return std::unexpected("YH-001 has three solder pads");
    pads_ = value;
    return {};
}

void Yh001::power_on() {
    power_on_ax5202p();
    outer_.fill(0);
}

void Yh001::reset() {
    outer_.fill(0);
    acknowledge_irq();
}

void Yh001::save_state(StateWriter& writer) const {
    writer.write<Word>(182U);
    writer.write(pads_);
    writer.write_array(outer_);
    Mmc3::save_state(writer);
}

void Yh001::load_state(StateReader& reader) {
    const auto tag = reader.read<Word>();
    const auto pads = reader.read<Byte>();
    decltype(outer_) outer{};
    reader.read_array(outer);
    if (!reader.ok() || tag != 182U || pads != pads_) {
        reader.fail();
        return;
    }
    Mmc3::load_state(reader);
    if (reader.ok())
        outer_ = outer;
}
} // namespace mapperbus::core
