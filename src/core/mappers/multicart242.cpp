#include "core/mappers/multicart242.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
// Original implementation from NESdev mapper 242 r19709. ET-113 uses C alone
// for chip selection; no global modulo across its asymmetric ROM chips.
Multicart242::Multicart242(const INesHeader& h, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), wram_(h.has_battery ? 8192U : 0U) {}
Result<void> Multicart242::validate_geometry(const INesHeader& h, std::size_t p, std::size_t c) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 242 supports submapper 0");
    if (c != 0U || (p != 524288U && p != 655360U) || (h.has_battery && p != 524288U))
        return std::unexpected(
            "Mapper 242 requires 512 KiB PRG or battery-free 640 KiB ET-113 and no CHR ROM");
    if (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != (h.has_battery ? 8192U : 0U) ||
                      h.chr_ram_size != 8192U || h.chr_nvram_size != 0U))
        return std::unexpected(
            "Mapper 242 requires 8 KiB volatile CHR RAM and optional 8 KiB battery WRAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 242 supports ordinary H/V CIRAM metadata");
    return {};
}
Byte Multicart242::read_prg(Address addr) {
    if (addr < 0x8000U)
        return addr >= 0x6000U && !wram_.empty() ? wram_[addr & 0x1FFFU] : 0U;
    const bool high = addr >= 0xC000U;
    unsigned inner = (latch_ >> 2U) & 7U;
    if ((latch_ & 1U) != 0U)
        inner = (inner & 6U) | static_cast<unsigned>(high);
    if (high && wram_.empty() && (latch_ & 0x80U) == 0U)
        inner = (latch_ & 0x200U) != 0U ? 7U : 0U;
    const unsigned outer = (latch_ >> 2U) & 0x18U;
    const bool replacement = prg_rom_.size() == 655360U && (latch_ & 0x400U) == 0U;
    unsigned offset = replacement ? 524288U + inner * 16384U : (outer | inner) * 16384U;
    offset += addr & 0x3FFFU;
    if ((latch_ & 0x100U) != 0U)
        offset = (offset & ~31U) | pads_;
    return prg_rom_[offset];
}
void Multicart242::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        latch_ = addr & 0x7FFU;
    else if (addr >= 0x6000U && !wram_.empty())
        wram_[addr & 0x1FFFU] = value;
}
bool Multicart242::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && !wram_.empty());
}
Byte Multicart242::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}
void Multicart242::write_chr(Address addr, Byte value) {
    if (!wram_.empty() || (latch_ & 0x80U) == 0U)
        chr_ram_[addr & 0x1FFFU] = value;
}
MirrorMode Multicart242::mirror_mode() const {
    return (latch_ & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}
Result<void> Multicart242::configure_board_jumpers(Byte value) {
    if (value > 31U)
        return std::unexpected("Mapper 242 menu solder pads require a value 0..31");
    pads_ = value;
    return {};
}
Result<void> Multicart242::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("menu-"))
        return std::unexpected("Mapper 242 board profiles are menu-0 through menu-31");
    profile.remove_prefix(5U);
    unsigned value = 0U;
    const auto [end, error] =
        std::from_chars(profile.data(), profile.data() + profile.size(), value);
    if (error != std::errc{} || end != profile.data() + profile.size() || value > 31U)
        return std::unexpected("Mapper 242 board profiles are menu-0 through menu-31");
    return configure_board_jumpers(static_cast<Byte>(value));
}
std::span<const Byte> Multicart242::battery_ram() const {
    return wram_;
}
void Multicart242::set_battery_ram(std::span<const Byte> data) {
    std::ranges::copy(data.first(std::min(data.size(), wram_.size())), wram_.begin());
}
void Multicart242::power_on() {
    reset();
    chr_ram_.fill(0U);
}
void Multicart242::reset() {
    latch_ = 0U;
}
uint16_t Multicart242::state_tag() const {
    return static_cast<uint16_t>(0xF200U | (pads_ << 2U) | (wram_.empty() ? 0U : 1U) |
                                 (prg_rom_.size() == 655360U ? 2U : 0U));
}
void Multicart242::save_state(StateWriter& w) const {
    w.write(state_tag());
    w.write(latch_);
    w.write_array(chr_ram_);
    if (!wram_.empty()) {
        std::array<Byte, 8192> ram{};
        std::ranges::copy(wram_, ram.begin());
        w.write_array(ram);
    }
}
void Multicart242::load_state(StateReader& r) {
    const auto tag = r.read<uint16_t>(), latch = r.read<Address>();
    std::array<Byte, 8192> chr{}, ram{};
    r.read_array(chr);
    if (!wram_.empty())
        r.read_array(ram);
    if (!r.ok() || tag != state_tag() || latch > 0x7FFU) {
        r.fail();
        return;
    }
    latch_ = latch;
    chr_ram_ = chr;
    if (!wram_.empty())
        std::ranges::copy(ram, wram_.begin());
}
} // namespace mapperbus::core
