#include "core/mappers/coolx_lite.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
INesHeader inner_header(INesHeader h) {
    h.mapper_number = 4;
    h.submapper = 0;
    return h;
}
} // namespace
CoolXLite::CoolXLite(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : Mmc3(inner_header(h), std::move(prg), std::move(chr)), work_ram_(h.prg_ram_size),
      pattern_ram_(h.chr_ram_size), sub_(h.submapper), prg_bytes_(physical_prg_size()),
      chr_bytes_(physical_chr_size()) {
    power_on();
}
Result<void> CoolXLite::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (!h.is_nes2 || h.mapper_number != 479 || (h.submapper != 1 && h.submapper != 2) ||
        h.has_battery || h.has_trainer || h.prg_nvram_size || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical) ||
        (h.prg_ram_size != 0 && h.prg_ram_size != 8192) || !std::has_single_bit(prg) ||
        prg < 32768 || prg > 512 * 1024)
        return std::unexpected("Unsupported CoolX Lite extract board or PRG ROM/RAM population");
    if (chr ? (!std::has_single_bit(chr) || chr < 8192 || chr > 256 * 1024 || h.chr_ram_size)
            : h.chr_ram_size != 8192)
        return std::unexpected("CoolX Lite requires CHR ROM or an 8 KiB CHR RAM");
    return {};
}
bool CoolXLite::maps_prg(Address addr) const {
    return addr >= 0x8000 || (addr >= 0x6000 && !work_ram_.empty());
}
Byte CoolXLite::read_prg(Address addr) {
    if (!maps_prg(addr))
        return 0;
    if (addr < 0x8000)
        return work_ram_[addr & 8191];
    const unsigned mask = ((outer_[5] & 31U) << 1U) | 1U;
    const unsigned base = ((outer_[1] & 31U) << 1U) | (outer_[2] << 6U);
    const unsigned mode = outer_[3] & 0x0C;
    if (mode == 4 || (outer_[4] & 0x80)) {
        const unsigned bank = (prg_register6() & (mask >> 2)) | (base >> 2);
        return physical_prg(bank * 32768U + (addr & 32767));
    }
    if (mode & 8) {
        const unsigned inner = addr < 0xC000 ? prg_register6() : 0xFF;
        const unsigned bank = (inner & (mask >> 1)) | (base >> 1);
        return physical_prg(bank * 16384U + (addr & 16383));
    }
    const unsigned bank = (prg_bank(addr) & mask) | base;
    return physical_prg(bank * 8192U + (addr & 8191));
}
void CoolXLite::write_prg(Address addr, Byte value) {
    if (addr < 0x8000) {
        if (maps_prg(addr))
            work_ram_[addr & 8191] = value;
        return;
    }
    if (outer_[3] & 0x0C)
        set_prg_register6(value);
    else {
        if ((addr & 0xE001) == 0xA000)
            mirror_reg_ = value & 3;
        Mmc3::write_prg(addr, value);
    }
}
void CoolXLite::write_expansion(Address addr, Byte value) {
    if (maps_expansion(addr) && !(outer_[3] & 2))
        outer_[addr - 0x4FF0] = value;
}
std::size_t CoolXLite::pattern_offset(Address addr) const {
    const unsigned mask = ((outer_[4] & 31U) << 3U) | 7U;
    return (chr_bank(addr) & mask) * 1024U + (addr & 1023);
}
Byte CoolXLite::read_chr(Address addr) {
    const auto offset = pattern_offset(addr);
    return pattern_ram_.empty() ? physical_chr(offset) : pattern_ram_[offset & 8191];
}
void CoolXLite::write_chr(Address addr, Byte value) {
    if (!pattern_ram_.empty() && !(outer_[3] & 1))
        pattern_ram_[pattern_offset(addr) & 8191] = value;
}
MirrorMode CoolXLite::mirror_mode() const {
    if ((outer_[3] & 0x0C) == 4)
        return prg_register6() & 0x10 ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    if ((outer_[4] & 0x40) && !(mirror_reg_ & 2))
        return mirror_reg_ & 1 ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    return mirror_reg_ & 1 ? MirrorMode::Horizontal : MirrorMode::Vertical;
}
void CoolXLite::power_on() {
    std::ranges::fill(work_ram_, 0);
    std::ranges::fill(pattern_ram_, 0);
    power_on_ax5202p();
    mirror_reg_ = 0;
    reset();
}
void CoolXLite::reset() {
    // The FPGA reloads its extract preset while the inner bank registers retain data.
    Mmc3::acknowledge_irq();
    outer_.fill(0);
    outer_[4] = sub_ == 1 ? 0x5F : 0x9F;
    outer_[5] = 0x1F;
}
void CoolXLite::save_state(StateWriter& w) const {
    w.write<Word>(479);
    w.write(sub_);
    w.write(prg_bytes_);
    w.write(chr_bytes_);
    w.write_array(outer_);
    w.write(mirror_reg_);
    w.write_bytes(work_ram_);
    w.write_bytes(pattern_ram_);
    Mmc3::save_state(w);
}
void CoolXLite::load_state(StateReader& r) {
    auto id = r.read<Word>();
    auto sub = r.read<Byte>();
    auto prg = r.read<uint32_t>();
    auto chr = r.read<uint32_t>();
    decltype(outer_) outer{};
    r.read_array(outer);
    auto mirror = r.read<Byte>();
    std::vector<Byte> work, pattern;
    r.read_bytes(work, 8192);
    r.read_bytes(pattern, 8192);
    if (!r.ok() || id != 479 || sub != sub_ || prg != prg_bytes_ || chr != chr_bytes_ ||
        mirror > 3 || work.size() != work_ram_.size() || pattern.size() != pattern_ram_.size()) {
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
    mirror_reg_ = mirror;
    work_ram_ = std::move(work);
    pattern_ram_ = std::move(pattern);
}
} // namespace mapperbus::core
