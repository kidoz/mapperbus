#include "core/mappers/mapper413.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Mapper413::Mapper413(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> chr_rom,
                     std::span<const Byte> serial)
    : prg_(std::move(prg_rom)), chr_(std::move(chr_rom)), samples_(serial.begin(), serial.end()),
      mirror_(header.mirror_mode) {}

Result<void> Mapper413::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (!h.is_nes2 || h.mapper_number != 413 || h.submapper != 0 || prg != 262144 ||
        chr != 262144 || h.has_battery || h.has_trainer || h.prg_ram_size || h.prg_nvram_size ||
        h.chr_ram_size || h.chr_nvram_size || h.misc_rom_count != 1 ||
        (h.mirror_mode != MirrorMode::Vertical && h.mirror_mode != MirrorMode::Horizontal))
        return std::unexpected(
            "Mapper 413 requires 256 KiB each of PRG and CHR ROM and one serial ROM");
    return {};
}

Byte Mapper413::read_sample() {
    const Byte value = samples_[state_.address];
    state_.address = (state_.address + state_.increment) & (kSerialSize - 1U);
    return value;
}
bool Mapper413::maps_expansion(Address addr) const {
    return addr >= 0x4800 && addr < 0x6000;
}
Byte Mapper413::read_expansion(Address addr) {
    if (!maps_expansion(addr))
        return 0;
    return addr < 0x5000 ? read_sample() : prg_[0x1000 + (addr & 0xFFF)];
}
bool Mapper413::maps_prg(Address addr) const {
    return addr >= 0x6000;
}
Byte Mapper413::read_prg(Address addr) {
    if (addr < 0x6000)
        return 0;
    if (addr < 0xC000) {
        const unsigned slot = (addr - 0x6000) / 0x2000;
        return prg_[(state_.banks[slot] & 31U) * 8192U + (addr & 0x1FFF)];
    }
    if (addr < 0xD000)
        return read_sample();
    if (addr < 0xE000)
        return prg_[7U * 4096 + (addr & 0xFFF)];
    return prg_[4U * 8192 + (addr & 0x1FFF)];
}

void Mapper413::write_prg(Address addr, Byte value) {
    switch (addr >> 12) {
    case 8:
        state_.latch = value;
        break;
    case 9:
        state_.counter = 0;
        break;
    case 10:
        state_.enabled = state_.pending = 0;
        break;
    case 11:
        state_.enabled = 1;
        break;
    case 12:
        state_.address = ((state_.address << 1) | (value >> 7)) & (kSerialSize - 1U);
        break;
    case 13:
        state_.increment = (value >> 1) & 1U;
        break;
    case 14:
    case 15:
        state_.banks[value >> 6] = value & 63U;
        break;
    default:
        break;
    }
}
Byte Mapper413::read_chr(Address addr) {
    const std::size_t page = addr < 0x1000 ? state_.banks[3] : 0x3D;
    return chr_[page * 4096 + (addr & 0xFFF)];
}
void Mapper413::observe_ppu_a12(bool high) {
    if (high && !state_.a12 && state_.low_cycles >= 5) {
        state_.counter = state_.counter == 0 ? state_.latch : static_cast<Byte>(state_.counter - 1);
        if (state_.counter == 0 && state_.enabled)
            state_.pending = 1;
    }
    state_.a12 = high;
    if (high)
        state_.low_cycles = 0;
}
void Mapper413::clock_cpu() {
    if (!state_.a12 && state_.low_cycles < 5)
        ++state_.low_cycles;
}
void Mapper413::save_state(StateWriter& w) const {
    w.write<std::uint16_t>(0x4130);
    w.write(mirror_);
    w.write_array(state_.banks);
    w.write(state_.address);
    w.write(state_.increment);
    w.write(state_.counter);
    w.write(state_.latch);
    w.write(state_.enabled);
    w.write(state_.pending);
    w.write(state_.a12);
    w.write(state_.low_cycles);
}
void Mapper413::load_state(StateReader& r) {
    const auto tag = r.read<std::uint16_t>();
    const auto mirror = r.read<MirrorMode>();
    Registers next;
    r.read_array(next.banks);
    r.read(next.address);
    r.read(next.increment);
    r.read(next.counter);
    r.read(next.latch);
    r.read(next.enabled);
    r.read(next.pending);
    r.read(next.a12);
    r.read(next.low_cycles);
    const bool banks_valid = std::ranges::all_of(next.banks, [](Byte b) { return b < 64; });
    if (!r.ok() || tag != 0x4130 || mirror != mirror_ || !banks_valid ||
        next.address >= kSerialSize || next.increment > 1 || next.enabled > 1 ||
        next.pending > next.enabled || next.a12 > 1 || next.low_cycles > 5 ||
        (next.a12 && next.low_cycles)) {
        r.fail();
        return;
    }
    state_ = next;
}
} // namespace mapperbus::core
