#include "core/mappers/taito_x1017.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev Taito X1-017 r24125 and krzysiobal's January 2020 hardware tests.
TaitoX1017::TaitoX1017(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)),
      physical_prg_(header.mapper_number == 552U), battery_(header.has_battery),
      initial_mirror_(header.mirror_mode == MirrorMode::Vertical ? 1U : 0U) {
    reset();
}

void TaitoX1017::reset() {
    // Stable emulator defaults; the physical RAM is retained and locked.
    prg_banks_ = {0U, 1U, 2U};
    chr_banks_.fill(0U);
    chr_mode_ = initial_mirror_;
    ram_enabled_.fill(0U);
    irq_latch_ = 0U;
    irq_control_ = 0U;
    irq_pending_ = 0U;
    irq_counter_ = 0U;
}

bool TaitoX1017::maps_prg(Address addr) const {
    if (addr >= 0x8000U)
        return true;
    if (addr < 0x6000U || addr >= 0x7400U)
        return false;
    return ram_enabled_[(addr - 0x6000U) / 0x800U] != 0U;
}

Byte TaitoX1017::cpu_open_bus_mask() const {
    // The ASIC pulls down the entire external CPU bus, including floating
    // portions of the console's APU/controller registers, not just its RAM.
    return 0U;
}

Byte TaitoX1017::read_prg(Address addr) {
    if (addr < 0x8000U)
        return maps_prg(addr) ? internal_ram_[addr - 0x6000U] : 0U;
    const auto slot = (addr - 0x8000U) / 0x2000U;
    const std::size_t bank = slot < 3U ? prg_banks_[slot] : prg_rom_.size() / 0x2000U - 1U;
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

Byte TaitoX1017::decode_prg(Byte value) const {
    if (!physical_prg_)
        return static_cast<Byte>((static_cast<unsigned>(value) >> 2U) & 15U);
    unsigned bank = 0U;
    for (unsigned bit = 0U; bit < 6U; ++bit)
        bank |= ((static_cast<unsigned>(value) >> bit) & 1U) << (5U - bit);
    return static_cast<Byte>(bank);
}

uint16_t TaitoX1017::irq_reload(bool acknowledge) const {
    if (irq_latch_ == 0U)
        return acknowledge ? 1U : 17U;
    return static_cast<uint16_t>((static_cast<unsigned>(irq_latch_) + (acknowledge ? 1U : 2U)) *
                                 16U);
}

void TaitoX1017::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x7400U) {
        if (maps_prg(addr))
            internal_ram_[addr - 0x6000U] = value;
        return;
    }
    // All address bits are decoded; unlike X1-005, A7 is connected.
    if (addr < 0x7EF0U || addr > 0x7EFFU)
        return;
    const auto reg = addr & 15U;
    if (reg < 6U) {
        chr_banks_[reg] = reg < 2U ? value & 0xFEU : value;
    } else if (reg == 6U) {
        chr_mode_ = value & 3U;
    } else if (reg < 10U) {
        constexpr std::array<Byte, 3> kRamKeys = {0xCAU, 0x69U, 0x84U};
        ram_enabled_[reg - 7U] = value == kRamKeys[reg - 7U] ? 1U : 0U;
    } else if (reg < 13U) {
        prg_banks_[reg - 10U] = decode_prg(value);
    } else if (reg == 13U) {
        irq_latch_ = value;
    } else if (reg == 14U) {
        irq_control_ = value & 7U;
        if ((irq_control_ & 1U) == 0U)
            irq_counter_ = irq_reload(false);
    } else {
        irq_pending_ = 0U;
        irq_counter_ = irq_reload(true);
    }
}

std::size_t TaitoX1017::chr_offset(Address address) const {
    const auto addr =
        (static_cast<unsigned>(address) & 0x1FFFU) ^ ((chr_mode_ & 2U) != 0U ? 0x1000U : 0U);
    if (addr < 0x1000U)
        return static_cast<std::size_t>(chr_banks_[addr / 0x800U]) * 0x400U + (addr & 0x7FFU);
    return static_cast<std::size_t>(chr_banks_[2U + (addr - 0x1000U) / 0x400U]) * 0x400U +
           (addr & 0x3FFU);
}

Byte TaitoX1017::read_chr(Address addr) {
    return chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void TaitoX1017::write_chr(Address, Byte) {}

MirrorMode TaitoX1017::mirror_mode() const {
    // M=0 routes PPU A11, M=1 routes PPU A10 to CIRAM A10.
    return (chr_mode_ & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

void TaitoX1017::clock_cpu() {
    // Bit 2's alternate clock source is unknown. Only mode 0 counts M2.
    // Post-expiry wrap is unobservable without acknowledging/reloading, so
    // keep a saturated counter and a separate latched pending flag.
    if ((irq_control_ & 5U) == 1U && irq_counter_ != 0U) {
        --irq_counter_;
        if (irq_counter_ == 0U)
            irq_pending_ = 1U;
    }
}

bool TaitoX1017::irq_pending() const {
    return irq_pending_ != 0U && (irq_control_ & 2U) != 0U;
}

std::span<const Byte> TaitoX1017::battery_ram() const {
    return battery_ ? std::span<const Byte>(internal_ram_) : std::span<const Byte>{};
}

void TaitoX1017::set_battery_ram(std::span<const Byte> data) {
    if (battery_)
        std::copy_n(
            data.begin(), std::min(data.size(), internal_ram_.size()), internal_ram_.begin());
}

void TaitoX1017::save_state(StateWriter& writer) const {
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(chr_mode_);
    writer.write_array(ram_enabled_);
    writer.write(irq_latch_);
    writer.write(irq_control_);
    writer.write(irq_pending_);
    writer.write(irq_counter_);
    writer.write_array(internal_ram_);
}

void TaitoX1017::load_state(StateReader& reader) {
    reader.read_array(prg_banks_);
    reader.read_array(chr_banks_);
    reader.read(chr_mode_);
    reader.read_array(ram_enabled_);
    reader.read(irq_latch_);
    reader.read(irq_control_);
    reader.read(irq_pending_);
    reader.read(irq_counter_);
    reader.read_array(internal_ram_);
    if (!reader.ok() || chr_mode_ > 3U || irq_control_ > 7U || irq_pending_ > 1U ||
        irq_counter_ > 4112U || (chr_banks_[0U] & 1U) != 0U || (chr_banks_[1U] & 1U) != 0U ||
        std::ranges::any_of(ram_enabled_, [](Byte enabled) { return enabled > 1U; }) ||
        std::ranges::any_of(prg_banks_,
                            [this](Byte bank) { return bank > (physical_prg_ ? 63U : 15U); })) {
        reset();
        reader.fail();
    }
}

} // namespace mapperbus::core
