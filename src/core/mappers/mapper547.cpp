#include "core/mappers/mapper547.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
Mapper547::Mapper547(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_(std::move(prg)), kanji_(std::move(chr)) {}
Result<void> Mapper547::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 547 || !h.is_nes2 || h.submapper || !h.has_battery || h.has_trainer ||
        prg != 655360 || chr != 131072 || h.prg_ram_size != 8192 || h.prg_nvram_size != 8192 ||
        h.chr_nvram_size || h.chr_ram_size != 8192 ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected("QTa requires 640/128 KiB PRG/Kanji ROM and 8 KiB each of volatile "
                               "WRAM, battery WRAM and CHR RAM");
    return {};
}
bool Mapper547::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}
unsigned Mapper547::ram_offset(Address addr) const {
    const Byte reg = regs_[(addr >> 12U) & 1U];
    return ((reg & 8U) << 10U) | ((reg & 1U) << 12U) | (addr & 0xFFFU);
}
Byte Mapper547::translated(Byte output) const {
    // The first 32-column / 16-row block starts at $20 for both JIS bytes.
    const unsigned row = regs_[13], col = regs_[12];
    if (row < 0x20 || col < 0x20)
        return 0;
    const unsigned x = col - 0x20U, y = row - 0x20U;
    const unsigned code = (x & 31U) + ((y & 15U) << 5U) + ((x >> 5U) << 9U) + (y >> 4U) * 1536U;
    constexpr std::array<Byte, 36> pages{0,  0,  2,  2,  1,  1,  4,  5,  6,  7,  8,  9,
                                         10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,  5,
                                         6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 13, 13};
    const unsigned tile = (((code & 255U) | (pages[code >> 8U] << 8U)) << 2U) | (regs_[11] & 3U);
    return output == 12 ? static_cast<Byte>(tile)
                        : static_cast<Byte>(0x40U | (tile >> 8U) | ((regs_[11] & 4U) << 5U));
}
Byte Mapper547::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0;
    if (addr < 0x8000U)
        return ram_[ram_offset(addr)];
    const auto page = addr >> 8U;
    if (page == 0xDC || page == 0xDD)
        return translated(page & 15U);
    unsigned bank = 79U;
    if (addr < 0xE000U) {
        const Byte reg = regs_[2U + ((addr - 0x8000U) >> 13U)];
        bank = (reg & 0x40U) ? 16U + (reg & 63U) : reg & 15U;
    }
    return prg_[bank * 8192U + (addr & 8191U)];
}
void Mapper547::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        ram_[ram_offset(addr)] = value;
        return;
    }
    if (addr < 0xD000 || addr >= 0xDE00)
        return;
    const auto reg = (addr >> 8U) & 15U;
    constexpr std::array<Byte, 14> masks{9, 9, 127, 127, 127, 1, 255, 255, 0, 3, 3, 7, 127, 127};
    regs_[reg] = value & masks[reg];
    if (reg == 8) {
        pending_ = false;
        regs_[9] = (regs_[9] & 1U) ? 3U : 0U;
    }
    if (reg == 9) {
        pending_ = false;
        if (regs_[9] & 2U)
            counter_ = regs_[6] | (regs_[7] << 8U);
    }
}
void Mapper547::clock_cpu() {
    if ((regs_[9] & 2U) && ++counter_ == 0) {
        pending_ = true;
        counter_ = regs_[6] | (regs_[7] << 8U);
    }
}
bool Mapper547::irq_pending() const {
    return pending_;
}
Byte Mapper547::read_chr(Address addr) {
    return chr_ram_[((addr & 0x1000U) || regs_[5] ? 4096U : 0U) + (addr & 4095U)];
}
void Mapper547::write_chr(Address addr, Byte value) {
    chr_ram_[((addr & 0x1000U) || regs_[5] ? 4096U : 0U) + (addr & 4095U)] = value;
}
unsigned Mapper547::nt_offset(Address addr) const {
    const unsigned bank = (regs_[10] & 2U) ? ((addr >> 11U) & 1U) : ((addr >> 10U) & 1U);
    return bank * 1024U + (addr & 1023U);
}
bool Mapper547::write_nametable(Address addr, Byte value) {
    if (!(regs_[10] & 1U))
        return false;
    qtram_[nt_offset(addr)] = value;
    return true;
}
std::optional<std::array<Byte, 2>> Mapper547::background_pattern(Address tile,
                                                                 Address pattern) const {
    const unsigned flags = qtram_[nt_offset(tile)];
    const unsigned off = pattern & 0xFF7U;
    if (!(flags & 0x40U)) {
        const unsigned base = (flags & 1U) << 12U;
        return std::array{chr_ram_[base + off], chr_ram_[base + off + 8U]};
    }
    const unsigned addr =
        ((flags & 63U) << 11U) | ((off & 0xFE0U) >> 1U) | ((off & 16U) >> 4U) | ((off & 7U) << 1U);
    return std::array{kanji_[addr], static_cast<Byte>((flags & 0x80U) ? 0xFFU : 0U)};
}
MirrorMode Mapper547::mirror_mode() const {
    return (regs_[10] & 2U) ? MirrorMode::Horizontal : MirrorMode::Vertical;
}
void Mapper547::reset() {
    regs_.fill(0);
    counter_ = 0;
    pending_ = false;
}
void Mapper547::power_on() {
    reset();
    std::fill(ram_.begin() + 8192, ram_.end(), 0);
    chr_ram_.fill(0);
    qtram_.fill(0);
}
std::span<const Byte> Mapper547::battery_ram() const {
    return std::span{ram_}.first(8192);
}
void Mapper547::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == 8192)
        std::ranges::copy(data, ram_.begin());
}
void Mapper547::save_state(StateWriter& w) const {
    w.write<Word>(547);
    w.write_array(regs_);
    w.write(counter_);
    w.write<Byte>(pending_);
    w.write_array(ram_);
    w.write_array(chr_ram_);
    w.write_array(qtram_);
}
void Mapper547::load_state(StateReader& r) {
    auto tag = r.read<Word>();
    decltype(regs_) regs{};
    r.read_array(regs);
    auto counter = r.read<Word>();
    auto pending = r.read<Byte>();
    decltype(ram_) ram{};
    decltype(chr_ram_) chr{};
    decltype(qtram_) qt{};
    r.read_array(ram);
    r.read_array(chr);
    r.read_array(qt);
    if (!r.ok() || tag != 547 || (regs[0] & ~9U) || (regs[1] & ~9U) ||
        (regs[2] | regs[3] | regs[4]) >= 128 || regs[5] > 1 || regs[8] || regs[9] > 3 ||
        regs[10] > 3 || regs[11] > 7 || regs[12] > 127 || regs[13] > 127 || pending > 1 ||
        (pending && !(regs[9] & 2U))) {
        r.fail();
        return;
    }
    regs_ = regs;
    counter_ = counter;
    pending_ = pending != 0;
    ram_ = ram;
    chr_ram_ = chr;
    qtram_ = qt;
}
} // namespace mapperbus::core
