#include "core/mappers/education_440_442.hpp"

#include <utility>

namespace mapperbus::core {
namespace {
unsigned reverse_bits(unsigned value, unsigned count) {
    unsigned result = 0;
    for (unsigned i = 0; i < count; ++i)
        result = (result << 1U) | ((value >> i) & 1U);
    return result;
}
constexpr std::array<Byte, 16> kProtection{0, 9, 0, 0, 0, 0, 0, 0x20, 4, 0, 0, 0, 0, 0, 0, 2};
} // namespace
Education440442::Education440442(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte>)
    : PecComputer(h.mapper_number == 440 ? 13 : 11), prg_(std::move(prg)), id_(h.mapper_number),
      mirror_(h.mirror_mode) {
    power_on();
}

Result<void> Education440442::validate_geometry(const INesHeader& h,
                                                std::size_t prg,
                                                std::size_t chr) {
    if (!h.is_nes2 || h.submapper || h.has_trainer || h.has_battery || chr ||
        h.prg_ram_size != 8192 || h.chr_ram_size != 8192 || h.prg_nvram_size || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected(
            "Computer requires NES 2.0 with 8 KiB each of volatile CPU and CHR RAM");
    if ((h.mapper_number == 440 && (prg == 96 * 1024 || prg == 128 * 1024)) ||
        (h.mapper_number == 442 && prg == 512 * 1024))
        return {};
    return std::unexpected("Unsupported Sonic REC-9388 or Golden Key PRG ROM population");
}
unsigned Education440442::prg_bank() const {
    return id_ == 440 ? (regs_.mode >> 5) & 3U : (regs_.mode & 7U) | ((regs_.mode >> 1) & 8U);
}
Byte Education440442::prg_read_mask(Address addr) const {
    if (addr < 0x6000 || (addr >= 0x8000 && prg_bank() * 32768U >= prg_.size()))
        return 0;
    return 0xFF;
}
Byte Education440442::read_prg(Address addr) {
    if (!prg_read_mask(addr))
        return 0;
    if (addr < 0x8000)
        return ram_[addr & 8191];
    const auto base = prg_bank() * 32768U;
    if (id_ == 440 && (regs_.mode & 1)) {
        const auto offset = (addr & 0x7000U) | reverse_bits(addr, 12);
        return static_cast<Byte>(reverse_bits(prg_[base + offset], 8));
    }
    return prg_[base + (addr & 0x7FFF)];
}
void Education440442::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000)
        ram_[addr & 8191] = value;
}
Byte Education440442::read_chr(Address addr) {
    if (regs_.mode & 0x80U)
        addr = (addr & 0x0FF7U) | ((regs_.pins & 1U) << 3U) | ((regs_.pins & 2U) << 11U);
    return chr_[addr & 8191];
}
void Education440442::write_chr(Address addr, Byte value) {
    chr_[addr & 8191] = value;
}
void Education440442::observe_ppu_read(Address addr) {
    if (id_ == 440 && addr >= 0x2000 && addr < 0x3F00 && (addr & 0x3FF) < 0x3C0)
        regs_.pins = (addr & 1U) | ((addr >> 8) & 2U);
}
void Education440442::observe_ppu_address(Address addr) {
    if (id_ != 442)
        return;
    const Byte pa13 = (addr >> 13) & 1;
    if (pa13 && !regs_.pa13)
        regs_.pins = (addr & 1U) | ((addr >> 8) & 2U);
    regs_.pa13 = pa13;
}
MirrorMode Education440442::mirror_mode() const {
    return id_ == 440 ? (regs_.auxiliary & 2U ? MirrorMode::Vertical : MirrorMode::Horizontal)
                      : mirror_;
}
Byte Education440442::expansion_read_mask(Address addr) const {
    if (id_ != 440 || !maps_expansion(addr))
        return 0;
    if ((addr & 0x700) == 0x300)
        return 0xFF;
    if ((addr & 0x700) == 0x500)
        return 0x27U | kProtection[regs_.index];
    return 0;
}
Byte Education440442::read_expansion(Address addr) {
    if (!expansion_read_mask(addr))
        return 0;
    if ((addr & 0x700) == 0x300) {
        regs_.status ^= 4U;
        return regs_.status;
    }
    return kProtection[regs_.index];
}
void Education440442::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr))
        return;
    switch (addr & 0x700U) {
    case 0:
        regs_.mode = value;
        break;
    case 0x100:
        if (id_ == 440)
            regs_.auxiliary = value & 2U;
        break;
    case 0x400:
        if (id_ == 440)
            regs_.index = value >> 4;
        break;
    }
}
void Education440442::power_on() {
    ram_.fill(0);
    chr_.fill(0);
    regs_ = {};
    regs_.mode = id_ == 440 ? 0x0E : 0;
    keyboard_.reset();
}
void Education440442::reset() {
    if (id_ == 442)
        regs_ = {};
    keyboard_.reset();
}
void Education440442::save_state(StateWriter& w) const {
    w.write(id_);
    w.write(mirror_);
    w.write(static_cast<uint32_t>(prg_.size()));
    w.write(regs_.mode);
    w.write(regs_.auxiliary);
    w.write(regs_.index);
    w.write(regs_.status);
    w.write(regs_.pins);
    w.write(regs_.pa13);
    keyboard_.save_state(w);
    w.write_array(ram_);
    w.write_array(chr_);
}
void Education440442::load_state(StateReader& r) {
    auto id = r.read<uint16_t>();
    auto mirror = r.read<MirrorMode>();
    auto size = r.read<uint32_t>();
    Registers regs;
    r.read(regs.mode);
    r.read(regs.auxiliary);
    r.read(regs.index);
    r.read(regs.status);
    r.read(regs.pins);
    r.read(regs.pa13);
    auto keyboard = keyboard_;
    keyboard.load_state(r);
    decltype(ram_) ram{}, chr{};
    r.read_array(ram);
    r.read_array(chr);
    if (!r.ok() || id != id_ || mirror != mirror_ || size != prg_.size() || (regs.auxiliary & ~2) ||
        regs.index > 15 || (regs.status & ~4) || regs.pins > 3 || regs.pa13 > 1 ||
        (id == 442 && (regs.auxiliary || regs.index || regs.status)) || (id == 440 && regs.pa13)) {
        r.fail();
        return;
    }
    regs_ = regs;
    ram_ = ram;
    chr_ = chr;
    keyboard_ = keyboard;
}
} // namespace mapperbus::core
