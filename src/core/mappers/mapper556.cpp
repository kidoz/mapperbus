#include "core/mappers/mapper556.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
Result<void> Mapper556::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 556 || !h.is_nes2 || h.submapper || h.has_battery || h.has_trainer ||
        (prg != 524288 && prg != 1048576 && prg != 3145728) || chr != prg ||
        (h.prg_ram_size && h.prg_ram_size != 8192) || h.prg_nvram_size || h.chr_ram_size ||
        h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected(
            "JY-215 needs matched 512 KiB, 1 MiB or 3 MiB ROMs and optional 8 KiB WRAM");
    return {};
}
Byte Mapper556::read_prg(Address addr) {
    if (addr < 0x8000)
        return Mmc3::read_prg(addr);
    unsigned inner = prg_bank(addr);
    if (vrc()) {
        const unsigned slot = ((addr >> 13U) & 3U);
        const std::array<unsigned, 4> banks{chip_.prg[0], chip_.prg[1], 0xFE, 0xFF};
        inner = banks[slot ^ ((chip_.swap && !(slot & 1U)) ? 2U : 0U)];
    }
    const unsigned bank = outer_[1] | ((outer_[2] & 0x40U) << 2U) | (inner & (~outer_[3] & 63U));
    return physical_prg(bank * 8192U + (addr & 8191U));
}
Byte Mapper556::read_chr(Address addr) {
    const unsigned inner = vrc() ? chip_.chr[(addr >> 10U) & 7U] : chr_bank(addr);
    const unsigned base = outer_[0] | ((outer_[2] & 0x30U) << 4U) | ((outer_[1] & 0x80U) << 3U) |
                          ((outer_[2] & 0x40U) << 5U);
    const unsigned mask = 0xFFU >> (15U - (outer_[2] & 15U));
    return physical_chr((base | (inner & mask)) * 1024U + (addr & 1023U));
}
MirrorMode Mapper556::mirror_mode() const {
    constexpr std::array modes{MirrorMode::Vertical,
                               MirrorMode::Horizontal,
                               MirrorMode::SingleLower,
                               MirrorMode::SingleUpper};
    return vrc() ? modes[chip_.mirror] : Mmc3::mirror_mode();
}
void Mapper556::write_expansion(Address addr, Byte value) {
    if (addr == 0x5000 && !(outer_[3] & 0x80U)) {
        outer_[index_] = value;
        index_ = (index_ + 1U) & 3U;
    }
}
void Mapper556::write_prg(Address addr, Byte value) {
    if (!vrc() || addr < 0x8000) {
        Mmc3::write_prg(addr, value);
        return;
    }
    const unsigned port = (addr >> 2U) & 3U;
    const unsigned page = addr >> 12U;
    if (page == 8 || page == 10)
        chip_.prg[page == 10] = value & 31U;
    else if (page == 9) {
        if (port < 2)
            chip_.mirror = value & 3U;
        else
            chip_.swap = (value >> 1U) & 1U;
    } else if (page < 15) {
        auto& bank = chip_.chr[(page - 11U) * 2U + (port >> 1U)];
        if (port & 1U)
            bank = (bank & 15U) | ((value & 31U) << 4U);
        else
            bank = (bank & 0x1F0U) | (value & 15U);
    } else if (port == 0)
        chip_.latch = (chip_.latch & 0xF0U) | (value & 15U);
    else if (port == 1)
        chip_.latch = (chip_.latch & 15U) | ((value & 15U) << 4U);
    else if (port == 2) {
        chip_.control = value & 7U;
        chip_.pending = 0;
        if (chip_.control & 2U) {
            chip_.counter = chip_.latch;
            chip_.prescaler = 341;
        }
    } else {
        chip_.pending = 0;
        chip_.control = (chip_.control & 5U) | ((chip_.control & 1U) << 1U);
    }
}
void Mapper556::clock_cpu() {
    if (!vrc() || !(chip_.control & 2U))
        return;
    if (!(chip_.control & 4U)) {
        if (chip_.prescaler > 3) {
            chip_.prescaler -= 3;
            return;
        }
        chip_.prescaler += 338;
    }
    if (++chip_.counter == 0) {
        chip_.counter = chip_.latch;
        chip_.pending = 1;
    }
}
void Mapper556::clock_irq_counter() {
    if (!vrc())
        Mmc3::clock_irq_counter();
}
bool Mapper556::irq_pending() const {
    return vrc() ? chip_.pending != 0 : Mmc3::irq_pending();
}
void Mapper556::acknowledge_irq() {
    if (vrc())
        chip_.pending = 0;
    else
        Mmc3::acknowledge_irq();
}
void Mapper556::reset() {
    Mmc3::reset();
    outer_ = {0, 0, 15, 0};
    index_ = 0;
    chip_ = {};
}
void Mapper556::save_state(StateWriter& w) const {
    w.write<Word>(556);
    w.write_array(outer_);
    w.write(index_);
    w.write_array(chip_.prg);
    w.write_array(chip_.chr);
    w.write(chip_.mirror);
    w.write(chip_.swap);
    w.write(chip_.latch);
    w.write(chip_.counter);
    w.write(chip_.control);
    w.write(chip_.pending);
    w.write(chip_.prescaler);
    Mmc3::save_state(w);
}
void Mapper556::load_state(StateReader& r) {
    auto tag = r.read<Word>();
    decltype(outer_) outer{};
    r.read_array(outer);
    auto index = r.read<Byte>();
    Vrc chip;
    r.read_array(chip.prg);
    r.read_array(chip.chr);
    r.read(chip.mirror);
    r.read(chip.swap);
    r.read(chip.latch);
    r.read(chip.counter);
    r.read(chip.control);
    r.read(chip.pending);
    r.read(chip.prescaler);
    if (!r.ok() || tag != 556 || index > 3 || ((outer[3] & 0x80U) && index) ||
        std::ranges::any_of(chip.prg, [](Byte v) { return v > 31; }) ||
        std::ranges::any_of(chip.chr, [](Word v) { return v > 511; }) || chip.mirror > 3 ||
        chip.swap > 1 || chip.control > 7 || chip.pending > 1 || !chip.prescaler ||
        chip.prescaler > 341) {
        r.fail();
        return;
    }
    Mmc3::load_state(r);
    if (r.ok()) {
        outer_ = outer;
        index_ = index;
        chip_ = chip;
    }
}
} // namespace mapperbus::core
