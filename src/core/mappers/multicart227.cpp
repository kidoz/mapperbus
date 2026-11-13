#include "core/mappers/multicart227.hpp"

#include <algorithm>
#include <charconv>

#include "core/state/state.hpp"

namespace mapperbus::core {

// Original implementation from NESdev mapper 227 r23188 and talk r14159.
// The article's population rule (no battery => multicart) resolves its otherwise
// conflicting submapper-zero CHR description. The RPG's absent UNROM circuitry
// is modeled as NROM selection regardless of O; see the compatibility notes.
Multicart227::Multicart227(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), wram_(header.has_battery ? 8192U : 0U),
      submapper_(header.is_nes2 ? header.submapper : 0U) {}

Result<void> Multicart227::validate_geometry(const INesHeader& h,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (h.is_nes2 && h.submapper > 2U)
        return std::unexpected("Mapper 227 supports submappers 0, 1 and 2");
    if (prg_size < 0x4000U || prg_size > 0x100000U || prg_size % 0x4000U != 0U || chr_size != 0U)
        return std::unexpected("Mapper 227 requires 16 KiB-1 MiB PRG ROM and no CHR ROM");
    if ((h.is_nes2 && h.submapper != 0U && h.has_battery) ||
        (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != (h.has_battery ? 8192U : 0U) ||
                       h.chr_ram_size != 8192U || h.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 227 requires 8 KiB volatile CHR RAM; only battery-backed "
                               "submapper 0 has 8 KiB PRG NVRAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 227 supports ordinary H/V CIRAM");
    return {};
}

bool Multicart227::maps_prg(Address addr) const {
    return addr >= 0x8000U || (!wram_.empty() && addr >= 0x6000U);
}

Byte Multicart227::read_prg(Address addr) {
    if (addr < 0x8000U)
        return addr >= 0x6000U && !wram_.empty() ? wram_[addr & 0x1FFFU] : 0U;
    unsigned inner = ((latch_ >> 4U) & 6U) | ((latch_ >> 2U) & 1U);
    if ((latch_ & 1U) != 0U)
        inner = (inner & 6U) | ((addr >> 14U) & 1U);
    if (addr >= 0xC000U && (latch_ & 0x80U) == 0U && wram_.empty())
        inner = (latch_ & 0x200U) != 0U ? 7U : 0U;
    unsigned outer = ((latch_ >> 3U) & 0x20U) | (latch_ & 0x18U);
    if (submapper_ == 2U && inner == 0U)
        outer &= 0x20U;
    unsigned offset = (outer | inner) * 0x4000U + (addr & 0x3FFFU);
    if (submapper_ == 1U && (latch_ & 0x400U) != 0U)
        offset = (offset & ~15U) | pads_;
    return prg_rom_[offset % prg_rom_.size()];
}

void Multicart227::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        latch_ = addr & 0x7FFU;
    else if (addr >= 0x6000U && !wram_.empty())
        wram_[addr & 0x1FFFU] = value;
}

Byte Multicart227::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Multicart227::write_chr(Address addr, Byte value) {
    if (!wram_.empty() || (latch_ & 0x80U) == 0U)
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Multicart227::mirror_mode() const {
    return (latch_ & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::span<const Byte> Multicart227::battery_ram() const {
    return wram_;
}

void Multicart227::set_battery_ram(std::span<const Byte> data) {
    std::ranges::copy(data.first(std::min(data.size(), wram_.size())), wram_.begin());
}

Result<void> Multicart227::configure_board_jumpers(Byte value) {
    if (submapper_ != 1U || value > 15U)
        return std::unexpected("Mapper 227 solder pads require submapper 1 and a value 0..15");
    pads_ = value;
    return {};
}

Result<void> Multicart227::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("menu-"))
        return std::unexpected("Mapper 227 menu profiles are menu-0 through menu-15");
    profile.remove_prefix(5U);
    unsigned value = 0U;
    const auto [end, error] =
        std::from_chars(profile.data(), profile.data() + profile.size(), value);
    if (error != std::errc{} || end != profile.data() + profile.size() || value > 15U)
        return std::unexpected("Mapper 227 menu profiles are menu-0 through menu-15");
    return configure_board_jumpers(static_cast<Byte>(value));
}

void Multicart227::reset() {
    latch_ = 0U; // Retain cartridge RAM and physical solder pads on console reset.
}

uint16_t Multicart227::state_tag() const {
    return static_cast<uint16_t>(0xE300U | submapper_ | (wram_.empty() ? 0U : 4U) | (pads_ << 4U));
}

void Multicart227::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(latch_);
    writer.write_array(chr_ram_);
    for (Byte value : wram_)
        writer.write(value);
}

void Multicart227::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto latch = reader.read<Address>();
    std::array<Byte, 8192> chr{};
    std::array<Byte, 8192> wram{};
    reader.read_array(chr);
    if (!wram_.empty())
        reader.read_array(wram);
    if (!reader.ok() || tag != state_tag() || latch > 0x7FFU) {
        reset();
        reader.fail();
        return;
    }
    latch_ = latch;
    chr_ram_ = chr;
    if (!wram_.empty())
        std::ranges::copy(wram, wram_.begin());
}

} // namespace mapperbus::core
