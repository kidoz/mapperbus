#include "core/mappers/chr_ram_multicarts.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
ChrRamMulticarts::ChrRamMulticarts(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte>)
    : prg_(std::move(prg)), id_(h.mapper_number), submapper_(h.submapper), mirror_(h.mirror_mode) {
    power_on();
}

Result<void> ChrRamMulticarts::validate_geometry(const INesHeader& h,
                                                 std::size_t prg,
                                                 std::size_t chr) {
    if (!h.is_nes2 || chr || h.has_battery || h.has_trainer || h.prg_ram_size || h.prg_nvram_size ||
        h.chr_ram_size != 8192 || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Vertical && h.mirror_mode != MirrorMode::Horizontal))
        return std::unexpected(
            "This multicart requires NES 2.0, ordinary CIRAM and only 8 KiB volatile CHR RAM");
    bool valid = false;
    switch (h.mapper_number) {
    case 277:
        valid = h.submapper == 0 && prg == 256 * 1024;
        break;
    case 280:
        valid = (h.submapper <= 1 && prg == 640 * 1024) || (h.submapper == 0 && prg == 512 * 1024);
        break;
    case 343:
        valid = (h.submapper == 0 && prg == 128 * 1024) || (h.submapper == 1 && prg == 1024 * 1024);
        break;
    case 301:
        valid = h.submapper == 0 && prg == 512 * 1024;
        break;
    case 363:
        valid = h.submapper == 0 && prg == 1024 * 1024;
        break;
    case 476:
        valid = h.submapper == 0 && prg == 512 * 1024;
        break;
    }
    if (!valid)
        return std::unexpected("Unsupported multicart PRG ROM or submapper");
    return {};
}

std::size_t ChrRamMulticarts::prg_page(Address addr) const {
    const bool upper = addr >= 0xC000;
    switch (id_) {
    case 277: {
        const unsigned page = latch_ & 15;
        if (!upper)
            return page;
        if ((page & 8) == 0)
            return 7;
        return page | 1;
    }
    case 280: {
        if (address_ & 0x100) {
            const unsigned bank = submapper_ == 1 ? address_ >> 2 : latch_;
            return 32U + (upper ? 7U : bank & 7U);
        }
        const unsigned page = (address_ >> 2) & 31;
        if ((address_ & 0x80) == 0)
            return upper ? 0 : page;
        if (address_ & 1)
            return (page & ~1U) | static_cast<unsigned>(upper);
        return page;
    }
    case 301: {
        const unsigned page = (address_ >> 2U) & 31U;
        if (!upper)
            return page;
        return (page & (address_ & 0x80 ? ~7U : ~0U)) | (address_ & 0x200 ? 7U : 0U);
    }
    case 363:
        return ((address_ & 7) << 3) | (upper ? 7 : latch_);
    case 343:
        return submapper_ == 1 ? 2U * latch_ + upper : latch_;
    default:
        return (address_ & 4) ? 2U * latch_ + upper : latch_;
    }
}
Byte ChrRamMulticarts::read_prg(Address addr) {
    if (id_ == 301 && (address_ & 0x100))
        addr = (addr & 0xFFFEU) | pads_;
    return addr < 0x8000 ? 0 : prg_[(prg_page(addr) * 16384 + (addr & 0x3FFF)) % prg_.size()];
}
void ChrRamMulticarts::write_prg(Address addr, Byte value) {
    if (addr < 0x8000)
        return;
    switch (id_) {
    case 277:
        if (!(latch_ & 0x20))
            latch_ = value & 0x3F;
        break;
    case 280:
        latch_ = value & read_prg(addr) & 7U;
        address_ = (address_ & 0x100) | (addr & 0xFF);
        break;
    case 301:
        address_ = addr & 0x3FE;
        break;
    case 363: {
        // A rising A4 at the write strobe transfers the outer block and CIRAM pins.
        if ((addr & 0x10) && !(address_ & 0x10))
            address_ = addr & 0x37;
        latch_ = value & 7;
        break;
    }
    case 343:
        latch_ = value;
        break;
    }
}
void ChrRamMulticarts::write_expansion(Address addr, Byte value) {
    if (id_ != 476 || (addr & 0xF000) != 0x5000)
        return;
    switch (addr & 0x300) {
    case 0:
        latch_ = value;
        break;
    case 0x200:
        address_ = value & 5;
        break;
    }
}
Byte ChrRamMulticarts::read_chr(Address addr) {
    return chr_[addr & 8191];
}
void ChrRamMulticarts::write_chr(Address addr, Byte value) {
    if (id_ != 280 || (address_ & 0x180) != 0x80)
        chr_[addr & 8191] = value;
}
MirrorMode ChrRamMulticarts::mirror_mode() const {
    switch (id_) {
    case 277:
        return (latch_ & 0x10) ? MirrorMode::Horizontal : MirrorMode::Vertical;
    case 301:
    case 280:
        return (address_ & 2) ? MirrorMode::Horizontal : MirrorMode::Vertical;
    case 363:
        return (address_ & 0x20) ? MirrorMode::Horizontal : MirrorMode::Vertical;
    case 476:
        return (address_ & 1) ? MirrorMode::Horizontal : MirrorMode::Vertical;
    default:
        return mirror_;
    }
}
void ChrRamMulticarts::power_on() {
    chr_.fill(0);
    address_ = 0;
    latch_ = id_ == 343 ? 0xFF : id_ == 277 ? 8 : 0;
}
void ChrRamMulticarts::reset() {
    if (id_ == 343)
        return; // Data latch has no console reset.
    address_ = id_ == 280 ? (address_ & 0x100) ^ 0x100 : 0;
    if (id_ != 280)
        latch_ = id_ == 277 ? 8 : 0;
}
Result<void> ChrRamMulticarts::configure_board_jumpers(Byte value) {
    if (id_ != 301)
        return Mapper::configure_board_jumpers(value);
    if (value > 1)
        return std::unexpected("8157 menu solder pad must be zero or one");
    pads_ = value;
    return {};
}
void ChrRamMulticarts::save_state(StateWriter& w) const {
    w.write(id_);
    w.write(submapper_);
    w.write(mirror_);
    if (id_ == 301)
        w.write(pads_);
    w.write(latch_);
    w.write(address_);
    w.write_array(chr_);
}
void ChrRamMulticarts::load_state(StateReader& r) {
    const auto id = r.read<std::uint16_t>();
    const auto sub = r.read<Byte>();
    const auto mirror = r.read<MirrorMode>();
    const bool matching_pad = id_ != 301 || r.read<Byte>() == pads_;
    const auto latch = r.read<Byte>();
    const auto address = r.read<std::uint16_t>();
    decltype(chr_) chr{};
    r.read_array(chr);
    const unsigned address_mask = id_ == 280   ? 0x1FF
                                  : id_ == 476 ? 5
                                  : id_ == 363 ? 0x37
                                  : id_ == 301 ? 0x3FE
                                               : 0;
    if (!matching_pad || !r.ok() || id != id_ || sub != submapper_ || mirror != mirror_ ||
        (address & ~address_mask) || (id_ == 277 && latch > 63) ||
        ((id_ == 280 || id_ == 363) && latch > 7)) {
        r.fail();
        return;
    }
    latch_ = latch;
    address_ = address;
    chr_ = chr;
}
} // namespace mapperbus::core
