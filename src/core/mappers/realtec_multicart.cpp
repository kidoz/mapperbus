#include "core/mappers/realtec_multicart.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

// Original address-pin decoding from NESdev mapper 236 r18875.
// Accept the three documented populations. Reset-zero and cold RAM clearing
// are deterministic policies; electrical reset/power-up is not specified.
RealtecMulticart::RealtecMulticart(const INesHeader&,
                                   std::vector<Byte> prg_rom,
                                   std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)),
      chr_(chr_rom.empty() ? std::vector<Byte>(8192U) : std::move(chr_rom)),
      chr_ram_(prg_rom_.size() == 524288U) {}

Result<void> RealtecMulticart::validate_geometry(const INesHeader& h,
                                                 std::size_t prg_size,
                                                 std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 236 supports submapper 0");
    const bool ram = prg_size == 524288U && chr_size == 0U;
    if (!ram && !(prg_size == 131072U && chr_size == 65536U) &&
        !(prg_size == 262144U && chr_size == 131072U))
        return std::unexpected("Mapper 236 requires 128/64 or 256/128 KiB PRG/CHR ROM, or "
                               "512 KiB PRG with 8 KiB CHR RAM");
    if (h.has_battery ||
        (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U ||
                       h.chr_ram_size != (ram ? 8192U : 0U) || h.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 236 memory must match the ROM or volatile CHR RAM board");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 236 supports ordinary H/V CIRAM metadata");
    return {};
}

Byte RealtecMulticart::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    unsigned bank = upper_ & (chr_ram_ ? 7U : 15U);
    if (chr_ram_)
        bank |= (lower_ & 7U) << 3U;
    const unsigned mode = (upper_ >> 4U) & 3U;
    if (mode < 2U && addr >= 0xC000U)
        bank |= 7U; // Force only A14..A16; retain the selected 128 KiB block.
    else if (mode == 2U)
        bank = (bank & ~1U) | ((addr >> 14U) & 1U);
    // Missing high address pins alias within the populated power-of-two chip.
    bank &= static_cast<unsigned>(prg_rom_.size() / 16384U - 1U);
    unsigned offset = bank * 16384U + (addr & 0x3FFFU);
    if (mode == 1U)
        offset = (offset & ~15U) | pads_;
    return prg_rom_[offset];
}

Byte RealtecMulticart::lower_mask() const {
    return chr_ram_ ? 0x27U : 0x2FU;
}
Byte RealtecMulticart::upper_mask() const {
    return chr_ram_ ? 0x37U : 0x3FU;
}
void RealtecMulticart::write_prg(Address addr, Byte /*value*/) {
    if (addr >= 0xC000U)
        upper_ = static_cast<Byte>(addr & upper_mask());
    else if (addr >= 0x8000U)
        lower_ = static_cast<Byte>(addr & lower_mask());
}
Byte RealtecMulticart::read_chr(Address addr) {
    const unsigned bank = chr_ram_ ? 0U : lower_ & 15U;
    const auto page = bank & (chr_.size() / 8192U - 1U);
    return chr_[page * 8192U + (addr & 0x1FFFU)];
}
void RealtecMulticart::write_chr(Address addr, Byte value) {
    if (chr_ram_)
        chr_[addr & 0x1FFFU] = value;
}
MirrorMode RealtecMulticart::mirror_mode() const {
    return (lower_ & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}
Result<void> RealtecMulticart::configure_board_jumpers(Byte value) {
    if (value > 15U)
        return std::unexpected("Mapper 236 solder pads require a value 0..15");
    pads_ = value;
    return {};
}
Result<void> RealtecMulticart::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("menu-"))
        return std::unexpected("Mapper 236 profiles are menu-0 through menu-15");
    profile.remove_prefix(5U);
    unsigned value = 0U;
    const auto [end, error] =
        std::from_chars(profile.data(), profile.data() + profile.size(), value);
    if (error != std::errc{} || end != profile.data() + profile.size() || value > 15U)
        return std::unexpected("Mapper 236 profiles are menu-0 through menu-15");
    return configure_board_jumpers(static_cast<Byte>(value));
}
void RealtecMulticart::power_on() {
    reset();
    if (chr_ram_)
        std::ranges::fill(chr_, 0U);
}
void RealtecMulticart::reset() {
    lower_ = 0U;
    upper_ = 0U;
}
uint16_t RealtecMulticart::state_tag() const {
    const unsigned population = chr_ram_ ? 2U : (prg_rom_.size() == 262144U ? 1U : 0U);
    return static_cast<uint16_t>(0xEC00U | (pads_ << 4U) | population);
}
void RealtecMulticart::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(lower_);
    writer.write(upper_);
    if (chr_ram_)
        for (Byte value : chr_)
            writer.write(value);
}
void RealtecMulticart::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto lower = reader.read<Byte>(), upper = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    if (chr_ram_)
        reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || (lower & ~lower_mask()) != 0U ||
        (upper & ~upper_mask()) != 0U) {
        reader.fail();
        return;
    }
    lower_ = lower;
    upper_ = upper;
    if (chr_ram_)
        std::ranges::copy(chr, chr_.begin());
}

} // namespace mapperbus::core
