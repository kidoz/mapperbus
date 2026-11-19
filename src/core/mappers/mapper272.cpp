#include "core/mappers/mapper272.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Byte kIrqTerminalCount = 86U;
} // namespace

Mapper272::Mapper272(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper272::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2)
        return std::unexpected("Mapper 272 requires a NES 2.0 header");
    if (header.submapper != 0U)
        return std::unexpected("Mapper 272 supports submapper 0");
    if (prg_size != 131072U || chr_size != 131072U)
        return std::unexpected("Mapper 272 requires 128 KiB PRG and 128 KiB CHR ROM");
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 272 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 272 requires H/V startup mirroring");
    return {};
}

Byte Mapper272::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const std::size_t bank_count = prg_rom_.size() / 0x2000U;
    const auto slot = static_cast<unsigned>(addr - 0x8000U) >> 13U;
    std::size_t bank = bank_count - 1U;
    if (slot == 0U)
        bank = prg_banks_[0U];
    else if (slot == 1U)
        bank = prg_banks_[1U];
    else if (slot == 2U)
        bank = bank_count - 2U;
    return prg_rom_[(bank % bank_count) * 0x2000U + (addr & 0x1FFFU)];
}

void Mapper272::write_vrc2(Address addr, Byte value) {
    const auto group = static_cast<unsigned>(addr) >> 12U;
    const auto port = static_cast<unsigned>(addr) & 3U;
    if (group == 8U || group == 10U) {
        prg_banks_[group == 8U ? 0U : 1U] = value & 0x1FU;
    } else if (group == 9U) {
        vrc_mirror_ = value & 1U;
    } else if (group >= 11U && group <= 14U) {
        auto& bank = chr_banks_[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U)
            bank = (bank & 0xF0U) | (value & 0x0FU);
        else
            bank = (bank & 0x0FU) | (value << 4U);
    }
}

void Mapper272::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;

    write_vrc2(addr, value);
    switch (addr & 0xC00CU) {
    case 0x8004U:
        pal_mirror_ = value & 3U;
        break;
    case 0x800CU:
        irq_pending_ = true;
        irq_counter_ = 0U;
        break;
    case 0xC004U:
        irq_pending_ = false;
        break;
    case 0xC008U:
        counter_enabled_ = true;
        break;
    case 0xC00CU:
        counter_enabled_ = false;
        irq_pending_ = false;
        irq_counter_ = 0U;
        break;
    default:
        break;
    }
}

Byte Mapper272::read_chr(Address addr) {
    const auto bank = chr_banks_[(static_cast<unsigned>(addr) >> 10U) & 7U];
    const std::size_t offset = static_cast<std::size_t>(bank) * 0x400U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper272::write_chr(Address, Byte) {}

MirrorMode Mapper272::mirror_mode() const {
    if (pal_mirror_ == 2U)
        return MirrorMode::SingleLower;
    if (pal_mirror_ == 3U)
        return MirrorMode::SingleUpper;
    return vrc_mirror_ != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper272::observe_ppu_address(Address addr) {
    const bool a13 = (addr & 0x2000U) != 0U;
    if (previous_a13_ && !a13 && counter_enabled_) {
        if (irq_counter_ + 1U == kIrqTerminalCount) {
            irq_counter_ = 0U;
            irq_pending_ = true;
        } else {
            ++irq_counter_;
        }
    }
    previous_a13_ = a13;
}

bool Mapper272::irq_pending() const {
    return irq_pending_;
}

void Mapper272::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper272::reset() {
    // The PAL's reset input is undocumented; these are deterministic console-reset defaults.
    prg_banks_.fill(0U);
    chr_banks_.fill(0U);
    vrc_mirror_ = initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
    pal_mirror_ = 0U;
    irq_counter_ = 0U;
    counter_enabled_ = false;
    irq_pending_ = false;
    previous_a13_ = false;
}

void Mapper272::power_on() {
    reset();
}

uint16_t Mapper272::state_tag() const {
    return static_cast<uint16_t>(0x2720U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper272::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(vrc_mirror_);
    writer.write(pal_mirror_);
    writer.write(irq_counter_);
    writer.write(static_cast<Byte>(counter_enabled_));
    writer.write(static_cast<Byte>(irq_pending_));
    writer.write(static_cast<Byte>(previous_a13_));
}

void Mapper272::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<Byte, 2> prg_banks{};
    std::array<Byte, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto vrc_mirror = reader.read<Byte>();
    const auto pal_mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto counter_enabled = reader.read<Byte>();
    const auto irq_pending = reader.read<Byte>();
    const auto previous_a13 = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag() || prg_banks[0U] > 0x1FU || prg_banks[1U] > 0x1FU ||
        vrc_mirror > 1U || pal_mirror > 3U || irq_counter >= kIrqTerminalCount ||
        counter_enabled > 1U || irq_pending > 1U || previous_a13 > 1U ||
        (counter_enabled == 0U && irq_counter != 0U)) {
        reader.fail();
        return;
    }
    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    vrc_mirror_ = vrc_mirror;
    pal_mirror_ = pal_mirror;
    irq_counter_ = irq_counter;
    counter_enabled_ = counter_enabled != 0U;
    irq_pending_ = irq_pending != 0U;
    previous_a13_ = previous_a13 != 0U;
}

} // namespace mapperbus::core
