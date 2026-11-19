#include "core/mappers/mapper548.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Mapper548::Mapper548(const INesHeader&, std::vector<Byte> prg, std::vector<Byte>)
    : prg_(std::move(prg)) {}

Result<void> Mapper548::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 548 || !h.is_nes2 || h.submapper || prg != 131072 || chr ||
        h.chr_ram_size != 8192 || h.prg_ram_size != 8192 || h.chr_nvram_size || h.prg_nvram_size ||
        h.has_battery || h.has_trainer || h.mirror_mode != MirrorMode::Horizontal)
        return std::unexpected(
            "CTC-15 requires 128 KiB PRG ROM, 8 KiB each of PRG/CHR RAM and horizontal mirroring");
    return {};
}

bool Mapper548::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}
Byte Mapper548::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return prg_[(addr >= 0xC000U ? 3U : bank_) * 16384U + (addr & 0x3FFFU)];
    return addr >= 0x6000U ? wram_[addr & 0x1FFFU] : 0U;
}
void Mapper548::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U)
        wram_[addr & 0x1FFFU] = value;
}
Byte Mapper548::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}
void Mapper548::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}
MirrorMode Mapper548::mirror_mode() const {
    return MirrorMode::Horizontal;
}

void Mapper548::write_expansion(Address addr, Byte) {
    switch (addr & 0xF800U) {
    case 0x4800U:
        latch_ = static_cast<Byte>(((addr >> 3U) & 4U) | ((addr >> 2U) & 3U));
        counting_ = (addr & 4U) == 0U;
        if (!counting_)
            phase_ = 0U;
        break;
    case 0x5000U:
        bank_ = latch_ ^ 5U;
        break;
    }
}
void Mapper548::clock_cpu_falling_edge() {
    // The IRQ gate observes divider bits 5, 2 and 0. Its period is 64 * 640 M2.
    if (counting_ && ++phase_ == 40960U)
        phase_ = 0U;
}
bool Mapper548::irq_pending() const {
    return ((phase_ / 640U) & 37U) == 37U;
}
void Mapper548::reset() {
    latch_ = bank_ = 7;
    phase_ = 0;
    counting_ = false;
}
void Mapper548::power_on() {
    reset();
    wram_.fill(0);
    chr_ram_.fill(0);
}
void Mapper548::save_state(StateWriter& w) const {
    w.write<Word>(548);
    w.write(bank_);
    w.write(latch_);
    w.write<Byte>(counting_);
    w.write(phase_);
    w.write_array(wram_);
    w.write_array(chr_ram_);
}
void Mapper548::load_state(StateReader& r) {
    const auto tag = r.read<Word>();
    auto bank = r.read<Byte>();
    auto latch = r.read<Byte>();
    auto counting = r.read<Byte>();
    auto phase = r.read<Word>();
    decltype(wram_) wram{}, chr_ram{};
    r.read_array(wram);
    r.read_array(chr_ram);
    if (!r.ok() || tag != 548 || bank > 7 || latch > 7 || counting > 1 || phase >= 40960 ||
        (!counting && phase)) {
        r.fail();
        return;
    }
    bank_ = bank;
    latch_ = latch;
    counting_ = counting != 0;
    phase_ = phase;
    wram_ = wram;
    chr_ram_ = chr_ram;
}
} // namespace mapperbus::core
