#include "core/mappers/mapper127.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Mapper127::Mapper127(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_(std::move(prg)), chr_(std::move(chr)) {
    power_on();
}

Result<void> Mapper127::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 127U || h.submapper != 0U || prg != 131072U || chr != 131072U ||
        h.has_battery || h.has_trainer || (h.is_nes2 && h.prg_ram_size != 0U) ||
        h.prg_nvram_size != 0U || h.chr_ram_size != 0U || h.chr_nvram_size != 0U ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected("Mapper 127 requires submapper 0 with 128 KiB PRG and CHR ROM");
    return {};
}
Byte Mapper127::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    return prg_[prg_banks_[(addr >> 13U) & 3U] * 8192U + (addr & 0x1FFFU)];
}
void Mapper127::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    const auto slot = addr & 3U;
    switch (addr & 0x70U) {
    case 0x00U:
        // PRG A15/A16 are tied high in the window containing the vectors.
        prg_banks_[slot] = slot == 3U ? (0x0CU | (value & 3U)) : (value & 15U);
        break;
    case 0x10U:
    case 0x20U:
        chr_banks_[((addr & 0x20U) ? 4U : 0U) + slot] = value & 127U;
        break;
    case 0x30U:
        enabled_ = true;
        break;
    case 0x40U:
        enabled_ = false;
        pending_ = false;
        counter_ = 0;
        break;
    case 0x50U:
        nametables_[slot] = value & 1U;
        break;
    }
}
Byte Mapper127::read_chr(Address addr) {
    return chr_[chr_banks_[(addr >> 10U) & 7U] * 1024U + (addr & 0x3FFU)];
}
void Mapper127::write_chr(Address, Byte) {}
MirrorMode Mapper127::mirror_mode() const {
    return MirrorMode::SingleLower;
}
std::optional<Address> Mapper127::nametable_address(Address addr) const {
    if (addr < 0x2000U || addr >= 0x3F00U)
        return std::nullopt;
    return static_cast<Address>(nametables_[(addr >> 10U) & 3U] * 1024U + (addr & 0x3FFU));
}
bool Mapper127::irq_pending() const {
    return pending_;
}
void Mapper127::clock_cpu() {
    if (enabled_ && --counter_ == 0U)
        pending_ = true;
}
void Mapper127::reset() {} // Latches and IRQ circuit retain their state on console reset.
void Mapper127::power_on() {
    prg_banks_.fill(15);
    chr_banks_.fill(0);
    nametables_.fill(0);
    counter_ = 0;
    enabled_ = false;
    pending_ = false;
}
void Mapper127::save_state(StateWriter& w) const {
    w.write<uint16_t>(127);
    w.write_array(prg_banks_);
    w.write_array(chr_banks_);
    w.write_array(nametables_);
    w.write(counter_);
    w.write(static_cast<Byte>(enabled_));
    w.write(static_cast<Byte>(pending_));
}
void Mapper127::load_state(StateReader& r) {
    const auto tag = r.read<uint16_t>();
    std::array<Byte, 4> prg{}, nt{};
    std::array<Byte, 8> chr{};
    r.read_array(prg);
    r.read_array(chr);
    r.read_array(nt);
    const auto count = r.read<Byte>();
    const auto enabled = r.read<Byte>();
    const auto pending = r.read<Byte>();
    if (!r.ok() || tag != 127U || *std::ranges::max_element(prg) > 15U || prg[3] < 12U ||
        *std::ranges::max_element(chr) > 127U || *std::ranges::max_element(nt) > 1U ||
        enabled > 1U || pending > enabled || (enabled == 0U && count != 0U)) {
        r.fail();
        return;
    }
    prg_banks_ = prg;
    chr_banks_ = chr;
    nametables_ = nt;
    counter_ = count;
    enabled_ = enabled != 0U;
    pending_ = pending != 0U;
}
} // namespace mapperbus::core
