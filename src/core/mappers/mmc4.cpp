#include "core/mappers/mmc4.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

// https://www.nesdev.org/wiki/MMC4 and MMC2's shared register description.
Mmc4::Mmc4(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), initial_mirror_(header.mirror_mode),
      mirror_(initial_mirror_) {
    reset();
}

void Mmc4::reset() {
    prg_bank_ = 0U;
    chr_banks_.fill(0U);
    latches_.fill(0U);
    mirror_ = initial_mirror_;
}

bool Mmc4::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mmc4::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;
    if (addr < 0x8000U)
        return prg_ram_[addr & 0x1FFFU];
    if (prg_rom_.empty())
        return 0U;
    const std::size_t bank = addr < 0xC000U ? prg_bank_ : prg_rom_.size() / 0x4000U - 1U;
    return prg_rom_[(bank * 0x4000U + (addr & 0x3FFFU)) % prg_rom_.size()];
}

void Mmc4::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[addr & 0x1FFFU] = value;
    } else if (addr >= 0xA000U && addr < 0xB000U) {
        prg_bank_ = value & 0x0FU;
    } else if (addr >= 0xB000U && addr < 0xF000U) {
        chr_banks_[(addr - 0xB000U) >> 12U] = value & 0x1FU;
    } else if (addr >= 0xF000U) {
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    }
}

Byte Mmc4::read_chr(Address addr) {
    addr &= 0x1FFFU;
    const auto half = addr >> 12U;
    const auto bank = chr_banks_[half * 2U + latches_[half]];
    const auto offset = bank * 0x1000U + (addr & 0x0FFFU);
    const auto value =
        chr_rom_.empty() ? chr_ram_[offset % chr_ram_.size()] : chr_rom_[offset % chr_rom_.size()];
    // The triggering fetch returns data from the old bank. Unlike MMC2's low
    // half, both halves of MMC4 decode the entire eight-byte FD/FE range.
    if ((addr & 0x0FF8U) == 0x0FD8U)
        latches_[half] = 0U;
    else if ((addr & 0x0FF8U) == 0x0FE8U)
        latches_[half] = 1U;
    return value;
}

void Mmc4::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty()) {
        addr &= 0x1FFFU;
        const auto half = addr >> 12U;
        const auto bank = chr_banks_[half * 2U + latches_[half]];
        chr_ram_[(bank * 0x1000U + (addr & 0x0FFFU)) % chr_ram_.size()] = value;
    }
}

MirrorMode Mmc4::mirror_mode() const {
    return mirror_;
}

void Mmc4::save_state(StateWriter& writer) const {
    writer.write(prg_bank_);
    writer.write_array(chr_banks_);
    writer.write_array(latches_);
    writer.write(mirror_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mmc4::load_state(StateReader& reader) {
    reader.read(prg_bank_);
    reader.read_array(chr_banks_);
    reader.read_array(latches_);
    reader.read(mirror_);
    reader.read_array(prg_ram_);
    reader.read_array(chr_ram_);
    if (prg_bank_ > 15U || latches_[0U] > 1U || latches_[1U] > 1U ||
        mirror_ > MirrorMode::SingleUpper ||
        std::ranges::any_of(chr_banks_, [](Byte bank) { return bank > 31U; })) {
        reset();
        reader.fail();
    }
}

std::span<const Byte> Mmc4::battery_ram() const {
    return prg_ram_;
}

void Mmc4::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), prg_ram_.size()), prg_ram_.begin());
}

} // namespace mapperbus::core
