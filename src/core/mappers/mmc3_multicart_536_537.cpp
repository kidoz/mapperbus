#include "core/mappers/mmc3_multicart_536_537.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
Mmc3Multicart536537::Mmc3Multicart536537(const INesHeader& h,
                                         std::vector<Byte> prg,
                                         std::vector<Byte> chr)
    : Mmc3(h, std::move(prg), std::move(chr)), id_(h.mapper_number),
      prg_size_(physical_prg_size()) {
    power_on();
}
Result<void> Mmc3Multicart536537::validate_geometry(const INesHeader& h,
                                                    std::size_t prg,
                                                    std::size_t chr) {
    if (!h.is_nes2 || h.submapper || h.has_battery || h.has_trainer || h.prg_ram_size ||
        h.prg_nvram_size || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected("N42S-2/JY4M4 requires NES 2.0 ROM-only PRG and ordinary CIRAM");
    if (h.mapper_number == 536 && prg == 1024 * 1024 && chr == 0 && h.chr_ram_size == 8192)
        return {};
    if (h.mapper_number == 537 && (prg == 384 * 1024 || prg == 512 * 1024) && chr == 512 * 1024 &&
        !h.chr_ram_size)
        return {};
    return std::unexpected("Unsupported N42S-2/JY4M4 ROM or CHR RAM population");
}
std::size_t Mmc3Multicart536537::prg_offset(Address addr) const {
    const Byte mode = outer_[0];
    const unsigned inner = prg_bank_pins(addr);
    unsigned page;
    if (id_ == 536) {
        const unsigned select = (mode & 31U) | ((mode >> 1U) & 32U);
        if (mode & 0x80U)
            page = ((select << 2U) | (inner & 15U)) & 127U;
        else if (mode & 0x20U)
            page = ((select & ~1U) << 1U) | ((addr >> 13U) & 3U);
        else
            page = (select << 1U) | ((addr >> 13U) & 1U);
    } else {
        const unsigned mask = (mode & 0x80U) ? 15U : 31U;
        const unsigned block = ((mode >> 1U) & 32U) | ((outer_[1] & 1U) << 4U);
        page = (inner & mask) | (block & ~mask);
    }
    return page * 8192U + (addr & 8191U);
}
Byte Mmc3Multicart536537::prg_read_mask(Address addr) const {
    // The 384 KiB population has three fitted 128 KiB PRG blocks.
    return addr >= 0x8000 && prg_offset(addr) < prg_size_ ? 0xFF : 0;
}
Byte Mmc3Multicart536537::read_prg(Address addr) {
    if (addr < 0x8000)
        return 0;
    const auto offset = prg_offset(addr);
    return offset < prg_size_ ? physical_prg(offset) : 0;
}
void Mmc3Multicart536537::write_prg(Address addr, Byte value) {
    if (addr < 0x8000) {
        if (addr >= 0x6000 && wram_write_enabled())
            outer_[addr & 1U] = value;
        return;
    }
    Mmc3::write_prg(addr, value);
}
Byte Mmc3Multicart536537::read_chr(Address addr) {
    if (id_ == 536)
        return Mmc3::read_chr(addr);
    const unsigned bank = ((outer_[0] & 0x40U) << 2U) | chr_bank(addr);
    return physical_chr(bank * 1024U + (addr & 1023U));
}
void Mmc3Multicart536537::power_on() {
    power_on_ax5202p();
    reset();
}
void Mmc3Multicart536537::reset() {
    outer_ = {static_cast<Byte>(id_ == 536 ? 0x60 : 0), 0};
    Mmc3::acknowledge_irq();
}
void Mmc3Multicart536537::save_state(StateWriter& w) const {
    w.write(id_);
    w.write(prg_size_);
    w.write_array(outer_);
    Mmc3::save_state(w);
}
void Mmc3Multicart536537::load_state(StateReader& r) {
    const auto id = r.read<Word>();
    const auto size = r.read<uint32_t>();
    decltype(outer_) outer{};
    r.read_array(outer);
    if (!r.ok() || id != id_ || size != prg_size_) {
        r.fail();
        return;
    }
    StateWriter backup;
    Mmc3::save_state(backup);
    Mmc3::load_state(r);
    if (!r.ok()) {
        StateReader restore(backup.data());
        Mmc3::load_state(restore);
        return;
    }
    outer_ = outer;
}
} // namespace mapperbus::core
