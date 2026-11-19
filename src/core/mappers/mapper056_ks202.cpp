#include "core/mappers/mapper056_ks202.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0x3800U;
} // namespace

// NESdev mapper 56 r4330, mapper 142 r5032, VRC3 r21300 and krzysiobal's
// reverse-engineering report in NESdev forum topic 19314.
Mapper056Ks202::Mapper056Ks202(const INesHeader& header,
                               std::vector<Byte> prg_rom,
                               std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper056Ks202::validate_geometry(const INesHeader& header,
                                               std::size_t prg_size,
                                               std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 056 supports submapper 0");
    if (prg_size != 262144U || chr_size != 131072U)
        return std::unexpected("Mapper 056 requires 256 KiB PRG ROM and 128 KiB CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 8192U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 0U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 056 requires only 8 KiB volatile PRG RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 056 supports ordinary H/V CIRAM");
    return {};
}

Byte Mapper056Ks202::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;
    if (addr < 0x8000U)
        return prg_ram_[addr & 0x1FFFU];

    const auto slot = (addr - 0x8000U) / 0x2000U;
    const auto low_bank = slot < 3U ? prg_banks_[slot] : 15U;
    const auto bank = static_cast<std::size_t>(low_bank | (prg_upper_[slot] << 4U));
    return prg_rom_[bank * 0x2000U + (addr & 0x1FFFU)];
}

void Mapper056Ks202::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    if (addr < 0xC000U) {
        const auto shift = ((addr >> 12U) - 8U) * 4U;
        irq_reload_ = static_cast<uint16_t>((irq_reload_ & ~(0xFU << shift)) |
                                            ((static_cast<unsigned>(value) & 15U) << shift));
        return;
    }
    if (addr < 0xD000U) {
        irq_control_ = value & 7U;
        irq_pending_ = false;
        if ((irq_control_ & 2U) != 0U)
            irq_counter_ = irq_reload_;
        return;
    }
    if (addr < 0xE000U) {
        irq_pending_ = false;
        return;
    }
    if (addr < 0xF000U) {
        bank_select_ = value & 7U;
        return;
    }

    if (addr < 0xF400U)
        prg_upper_[addr & 3U] = (value >> 4U) & 1U;
    if ((addr & 0xFC00U) == 0xF800U)
        mirror_ = (value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    if ((addr & 0xFC00U) == 0xFC00U)
        chr_banks_[addr & 7U] = value & 0x7FU;

    // Selectors 4 and 6 are documented no-ops. The behavior of 0, 5 and 7 is
    // unknown, so they likewise never invent a PRG mapping.
    if (bank_select_ >= 1U && bank_select_ <= 3U)
        prg_banks_[bank_select_ - 1U] = value & 15U;
}

bool Mapper056Ks202::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper056Ks202::read_chr(Address addr) {
    const auto offset = static_cast<std::size_t>(addr) & 0x1FFFU;
    const auto bank = static_cast<std::size_t>(chr_banks_[offset / 0x400U]);
    return chr_rom_[bank * 0x400U + (offset & 0x3FFU)];
}

void Mapper056Ks202::write_chr(Address, Byte) {}

MirrorMode Mapper056Ks202::mirror_mode() const {
    return mirror_;
}

void Mapper056Ks202::power_on() {
    reset();
    prg_ram_.fill(0U);
}

void Mapper056Ks202::reset() {
    // Only the four external PRG-A17 latches have a documented power-up value.
    // Other reset values and volatile-RAM clearing are deterministic policies.
    prg_banks_.fill(0U);
    prg_upper_.fill(1U);
    chr_banks_.fill(0U);
    bank_select_ = 0U;
    mirror_ = initial_mirror_;
    irq_reload_ = 0U;
    irq_counter_ = 0U;
    irq_control_ = 0U;
    irq_pending_ = false;
}

void Mapper056Ks202::clock_cpu() {
    if ((irq_control_ & 2U) == 0U)
        return;

    if ((irq_control_ & 4U) != 0U) {
        const auto low = static_cast<unsigned>(irq_counter_ & 0xFFU);
        if (low == 0xFFU) {
            irq_counter_ = static_cast<uint16_t>((irq_counter_ & 0xFF00U) | (irq_reload_ & 0xFFU));
            irq_pending_ = true;
        } else {
            irq_counter_ = static_cast<uint16_t>((irq_counter_ & 0xFF00U) | (low + 1U));
        }
    } else if (irq_counter_ == 0xFFFFU) {
        irq_counter_ = irq_reload_;
        irq_pending_ = true;
    } else {
        ++irq_counter_;
    }
}

bool Mapper056Ks202::irq_pending() const {
    return irq_pending_;
}

void Mapper056Ks202::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper056Ks202::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write_array(prg_banks_);
    writer.write_array(prg_upper_);
    writer.write_array(chr_banks_);
    writer.write(bank_select_);
    writer.write(static_cast<Byte>(mirror_ == MirrorMode::Vertical ? 1U : 0U));
    writer.write(irq_reload_);
    writer.write(irq_counter_);
    writer.write(irq_control_);
    writer.write(static_cast<Byte>(irq_pending_));
    writer.write_array(prg_ram_);
}

void Mapper056Ks202::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<Byte, 3> prg_banks{};
    std::array<Byte, 4> prg_upper{};
    std::array<Byte, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(prg_upper);
    reader.read_array(chr_banks);
    const auto bank_select = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    const auto irq_reload = reader.read<uint16_t>();
    const auto irq_counter = reader.read<uint16_t>();
    const auto irq_control = reader.read<Byte>();
    const auto irq_pending = reader.read<Byte>();
    std::array<Byte, 8192> prg_ram{};
    reader.read_array(prg_ram);

    if (!reader.ok() || tag != kStateTag ||
        !std::ranges::all_of(prg_banks, [](Byte bank) { return bank <= 15U; }) ||
        !std::ranges::all_of(prg_upper, [](Byte bank) { return bank <= 1U; }) ||
        !std::ranges::all_of(chr_banks, [](Byte bank) { return bank <= 127U; }) ||
        bank_select > 7U || mirror > 1U || irq_control > 7U || irq_pending > 1U) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    prg_upper_ = prg_upper;
    chr_banks_ = chr_banks;
    bank_select_ = bank_select;
    mirror_ = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    irq_reload_ = irq_reload;
    irq_counter_ = irq_counter;
    irq_control_ = irq_control;
    irq_pending_ = irq_pending != 0U;
    prg_ram_ = prg_ram;
}

} // namespace mapperbus::core
