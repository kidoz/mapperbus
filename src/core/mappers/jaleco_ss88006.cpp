#include "core/mappers/jaleco_ss88006.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
Byte with_nibble(Byte bank, Byte value, bool high) {
    const auto nibble = static_cast<unsigned>(value) & 15U;
    const auto previous = static_cast<unsigned>(bank);
    return static_cast<Byte>(high ? (previous & 15U) | (nibble << 4U)
                                  : (previous & 0xF0U) | nibble);
}
} // namespace

// NESdev mapper 18 r3918: $F003 address mask, nibble registers and M2 IRQ.
JalecoSs88006::JalecoSs88006(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)),
      prg_ram_size_(header.prg_ram_size + header.prg_nvram_size),
      nvram_size_(header.prg_nvram_size),
      initial_mirror_(header.mirror_mode == MirrorMode::Vertical ? 1U : 0U) {
    reset();
}

void JalecoSs88006::reset() {
    // Deterministic startup policy, not a claim about ASIC power-up values.
    // Retain RAM across reset, but require software to enable CPU access again.
    prg_banks_ = {0U, 1U, 2U};
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    ram_control_ = 0U;
    irq_reload_ = 0U;
    irq_counter_ = 0U;
    irq_control_ = 0U;
    irq_pending_ = 0U;
}

bool JalecoSs88006::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && prg_ram_size_ != 0U && (ram_control_ & 1U) != 0U);
}

Byte JalecoSs88006::read_prg(Address addr) {
    if (addr < 0x8000U)
        return maps_prg(addr) ? prg_ram_[(addr & 0x1FFFU) % prg_ram_size_] : 0U;
    const auto slot = (addr - 0x8000U) / 0x2000U;
    const std::size_t bank = slot < 3U ? prg_banks_[slot] : prg_rom_.size() / 0x2000U - 1U;
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void JalecoSs88006::write_prg(Address address, Byte value) {
    const auto addr = static_cast<unsigned>(address);
    if (addr < 0x8000U) {
        if (addr >= 0x6000U && prg_ram_size_ != 0U && (ram_control_ & 3U) == 3U)
            prg_ram_[(addr & 0x1FFFU) % prg_ram_size_] = value;
        return;
    }
    const auto reg = addr & 0xF003U;
    if (reg <= 0x9001U) {
        const auto slot = reg < 0x9000U ? (reg & 2U) / 2U : 2U;
        prg_banks_[slot] = with_nibble(prg_banks_[slot], value, (reg & 1U) != 0U) & 0x3FU;
    } else if (reg == 0x9002U) {
        ram_control_ = value & 3U;
    } else if (reg >= 0xA000U && reg <= 0xD003U) {
        const auto slot = ((reg >> 12U) - 10U) * 2U + (reg & 2U) / 2U;
        chr_banks_[slot] = with_nibble(chr_banks_[slot], value, (reg & 1U) != 0U);
    } else if (reg >= 0xE000U && reg <= 0xE003U) {
        const auto shift = (reg & 3U) * 4U;
        irq_reload_ = static_cast<Word>((static_cast<unsigned>(irq_reload_) & ~(15U << shift)) |
                                        ((static_cast<unsigned>(value) & 15U) << shift));
    } else if (reg == 0xF000U) {
        irq_counter_ = irq_reload_;
        irq_pending_ = 0U;
    } else if (reg == 0xF001U) {
        irq_control_ = value & 15U;
        irq_pending_ = 0U;
    } else if (reg == 0xF002U) {
        mirror_ = value & 3U;
    }
    // $9003 is unused. $F003 drives an optional external speech chip;
    // the SS88006 itself has no audio generator or readable status register.
}

Byte JalecoSs88006::read_chr(Address addr) {
    const auto offset = static_cast<unsigned>(addr) & 0x1FFFU;
    const auto bank = static_cast<std::size_t>(chr_banks_[offset / 0x400U]);
    return chr_rom_[(bank * 0x400U + (offset & 0x3FFU)) % chr_rom_.size()];
}

void JalecoSs88006::write_chr(Address, Byte) {}

MirrorMode JalecoSs88006::mirror_mode() const {
    constexpr std::array<MirrorMode, 4> kModes = {MirrorMode::Horizontal,
                                                  MirrorMode::Vertical,
                                                  MirrorMode::SingleLower,
                                                  MirrorMode::SingleUpper};
    return kModes[mirror_];
}

void JalecoSs88006::clock_cpu() {
    if ((irq_control_ & 1U) == 0U)
        return;
    const unsigned mask = (irq_control_ & 8U) != 0U   ? 0xFU
                          : (irq_control_ & 4U) != 0U ? 0xFFU
                          : (irq_control_ & 2U) != 0U ? 0xFFFU
                                                      : 0xFFFFU;
    const auto counter = static_cast<unsigned>(irq_counter_);
    const auto active = counter & mask;
    irq_counter_ = static_cast<Word>((counter & ~mask) | ((active - 1U) & mask));
    if (active == 0U)
        irq_pending_ = 1U;
}

bool JalecoSs88006::irq_pending() const {
    return irq_pending_ != 0U;
}

std::span<const Byte> JalecoSs88006::battery_ram() const {
    return std::span<const Byte>(prg_ram_).subspan(prg_ram_size_ - nvram_size_, nvram_size_);
}

void JalecoSs88006::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(),
                std::min(data.size(), nvram_size_),
                prg_ram_.begin() + (prg_ram_size_ - nvram_size_));
}

void JalecoSs88006::save_state(StateWriter& writer) const {
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(mirror_);
    writer.write(ram_control_);
    writer.write(irq_reload_);
    writer.write(irq_counter_);
    writer.write(irq_control_);
    writer.write(irq_pending_);
    writer.write_array(prg_ram_);
}

void JalecoSs88006::load_state(StateReader& reader) {
    reader.read_array(prg_banks_);
    reader.read_array(chr_banks_);
    reader.read(mirror_);
    reader.read(ram_control_);
    reader.read(irq_reload_);
    reader.read(irq_counter_);
    reader.read(irq_control_);
    reader.read(irq_pending_);
    reader.read_array(prg_ram_);
    if (!reader.ok() || mirror_ > 3U || ram_control_ > 3U || irq_control_ > 15U ||
        irq_pending_ > 1U ||
        std::ranges::any_of(prg_banks_, [](Byte bank) { return bank > 63U; })) {
        reset();
        reader.fail();
    }
}

} // namespace mapperbus::core
