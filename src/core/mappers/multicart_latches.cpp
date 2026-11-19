#include "core/mappers/multicart_latches.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
MulticartLatches::MulticartLatches(const INesHeader& h,
                                   std::vector<Byte> prg,
                                   std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), chr_ram_(h.chr_ram_size),
      id_(h.mapper_number) {}

Result<void> MulticartLatches::validate_geometry(const INesHeader& h,
                                                 std::size_t prg,
                                                 std::size_t chr) {
    if ((!h.is_nes2 && h.mapper_number != 204U) || h.submapper || h.has_trainer || h.has_battery ||
        (h.is_nes2 && h.prg_ram_size) || h.prg_nvram_size || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected("Unsupported discrete multicart header");
    switch (h.mapper_number) {
    case 204:
        if (!h.chr_ram_size &&
            ((prg == 65536 && chr == 32768) || (prg == 131072 && (chr == 57344 || chr == 65536))))
            return {};
        break;
    case 428:
        if (prg == 131072U && chr == 65536U && !h.chr_ram_size)
            return {};
        break;
    case 459:
        if (prg == 262144U && chr == 131072U && !h.chr_ram_size)
            return {};
        break;
    case 462:
        if (prg == 524288U && !chr && h.chr_ram_size == 8192U)
            return {};
        break;
    case 541:
        if (std::has_single_bit(prg) && prg >= 32768U && prg <= 1048576U && !chr &&
            h.chr_ram_size == 8192U)
            return {};
        break;
    }
    return std::unexpected("Unsupported discrete multicart ROM or RAM population");
}

bool MulticartLatches::maps_prg(Address addr) const {
    return addr >= (id_ == 428U ? 0x6000U : 0x8000U);
}
Byte MulticartLatches::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : maps_prg(addr) ? 3U : 0U;
}
Byte MulticartLatches::read_prg(Address addr) {
    if (addr < 0x8000U)
        return maps_prg(addr) ? jumpers_ : 0U;
    const unsigned half = (addr >> 14U) & 1U;
    unsigned bank;
    switch (id_) {
    case 204:
        if ((address_ & 0xFF0FU) == 0xF008U)
            return upper_jumpers_;
        if ((address_ & 0xFF0FU) == 0xF004U && prg_rom_.size() == 65536U)
            return jumpers_;
        bank = (address_ & 0x20U) ? (address_ & 14U) | half : address_ & 15U;
        break;
    case 428:
        bank = outer_ >> 5U;
        if (outer_ & 0x10U)
            bank = (bank & 6U) | half;
        break;
    case 459:
        bank = (address_ >> 4U) & 14U;
        if (half)
            bank |= (address_ & 4U) ? 1U : 7U;
        break;
    case 462:
        if (outer_ & 0x40U)
            bank = 16U | ((inner_ & 7U) << 1U) | half;
        else
            bank = ((outer_ >> 2U) & 24U) | (half ? 7U : inner_ & 7U);
        break;
    default: // LittleCom 160
        bank = address_ >> 2U;
        if (!(address_ & 2U))
            bank = (bank & ~1U) | half;
    }
    return prg_rom_[(bank * 16384U + (addr & 0x3FFFU)) % prg_rom_.size()];
}
void MulticartLatches::write_prg(Address addr, Byte data) {
    switch (id_) {
    case 204:
        if (addr >= 0x8000U)
            address_ = addr;
        break;
    case 428:
        if (addr >= 0x8000U)
            inner_ = data & 3U;
        else if (addr >= 0x6000U) {
            if ((addr & 3U) == 1U)
                outer_ = data;
            if ((addr & 3U) == 2U)
                chr_mask_ = data >> 6U;
        }
        break;
    case 459:
        if (addr >= 0x8000U)
            address_ = addr & 0x01FFU;
        break;
    case 462:
        if ((addr & 0xE000U) == 0xA000U)
            outer_ = data & 0x70U;
        else if (addr >= 0x8000U)
            inner_ = data & 0x17U;
        break;
    case 541:
        if (addr >= 0xC000U)
            address_ = addr & 0xFFU;
        break;
    }
}
MirrorMode MulticartLatches::mirror_mode() const {
    switch (id_) {
    case 204:
        return address_ & 16U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    case 428:
        return outer_ & 8U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    case 459:
        return address_ & 0x100U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    case 462:
        if (outer_ & 0x40U)
            return inner_ & 16U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
        return outer_ & 16U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    default:
        return address_ & 1U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    }
}
Byte MulticartLatches::read_chr(Address addr) {
    if (!chr_ram_.empty())
        return chr_ram_[addr & 0x1FFFU];
    if (id_ == 204) {
        const unsigned bank = address_ & (address_ & 32U ? 14U : 15U);
        const auto chip_mask = std::bit_ceil(chr_rom_.size()) - 1;
        const auto offset = (bank * 8192U + (addr & 8191U)) & chip_mask;
        return offset < chr_rom_.size() ? chr_rom_[offset] : 0xFF;
    }
    const unsigned bank = id_ == 428U
                              ? ((outer_ & 7U) & ~chr_mask_) | (inner_ & chr_mask_)
                              : (address_ & 3U) | ((address_ >> 2U) & 4U) | ((address_ >> 4U) & 8U);
    return chr_rom_[bank * 8192U + (addr & 0x1FFFU)];
}
void MulticartLatches::write_chr(Address addr, Byte value) {
    if (!chr_ram_.empty())
        chr_ram_[addr & 0x1FFFU] = value;
}
Result<void> MulticartLatches::configure_board_jumpers(Byte value) {
    if (id_ == 204) {
        if (prg_rom_.size() == 65536)
            jumpers_ = value;
        else
            upper_jumpers_ = value;
        return {};
    }
    if (id_ != 428U || value > 3U)
        return std::unexpected("Only mapper 428 has two menu jumpers");
    jumpers_ = value;
    return {};
}
Result<void> MulticartLatches::configure_board_profile(std::string_view profile) {
    if (id_ != 204 || !profile.starts_with("menu-"))
        return Mapper::configure_board_profile(profile);
    profile.remove_prefix(5);
    unsigned pins = 0;
    const auto end = profile.data() + profile.size();
    const auto [last, error] = std::from_chars(profile.data(), end, pins);
    if (error != std::errc{} || last != end || pins > 65535)
        return std::unexpected("Mapper 204 expects menu-0 through menu-65535");
    jumpers_ = static_cast<Byte>(pins);
    upper_jumpers_ = static_cast<Byte>(pins >> 8U);
    return {};
}
void MulticartLatches::reset() {
    address_ = 0;
    outer_ = inner_ = chr_mask_ = 0;
}
void MulticartLatches::power_on() {
    reset();
    std::ranges::fill(chr_ram_, 0);
}
void MulticartLatches::save_state(StateWriter& w) const {
    w.write(id_);
    w.write<std::uint32_t>(prg_rom_.size());
    if (id_ == 204) {
        w.write<std::uint32_t>(chr_rom_.size());
        w.write(upper_jumpers_);
    }
    w.write(jumpers_);
    w.write(address_);
    w.write(outer_);
    w.write(inner_);
    w.write(chr_mask_);
    w.write_bytes(chr_ram_);
}
void MulticartLatches::load_state(StateReader& r) {
    const auto id = r.read<std::uint16_t>();
    const auto size = r.read<std::uint32_t>();
    if (id_ == 204 &&
        (r.read<std::uint32_t>() != chr_rom_.size() || r.read<Byte>() != upper_jumpers_)) {
        r.fail();
        return;
    }
    const auto jumpers = r.read<Byte>();
    const auto address = r.read<Address>();
    const auto outer = r.read<Byte>();
    const auto inner = r.read<Byte>();
    const auto chr_mask = r.read<Byte>();
    std::vector<Byte> chr;
    r.read_bytes(chr, chr_ram_.size());
    const bool valid =
        id_ == 204U   ? (!address || address >= 0x8000U) && !outer && !inner && !chr_mask
        : id_ == 428U ? address == 0 && inner <= 3 && chr_mask <= 3
        : id_ == 462U ? address == 0 && !(outer & ~0x70U) && !(inner & ~0x17U) && !chr_mask
                      : !outer && !inner && !chr_mask && address <= (id_ == 459 ? 0x1FFU : 0xFFU);
    if (!r.ok() || id != id_ || size != prg_rom_.size() || jumpers != jumpers_ || !valid ||
        chr.size() != chr_ram_.size()) {
        r.fail();
        return;
    }
    address_ = address;
    outer_ = outer;
    inner_ = inner;
    chr_mask_ = chr_mask;
    chr_ram_.swap(chr);
}
} // namespace mapperbus::core
