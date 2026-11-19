#include "core/mappers/mapper106.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

// Original implementation from NESdev mapper 106 r4815 and Krzysiobal's linked
// board schematic. The board has no register reset inputs; cold-power values
// below are deterministic policies for the documented 74LS670 all-high tendency.
Mapper106::Mapper106(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(initial_mirror_) {
    power_on();
}

Result<void> Mapper106::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 106 supports submapper 0");
    if (prg_size != 0x40000U || chr_size != 0x20000U)
        return std::unexpected("Mapper 106 requires 256 KiB PRG and 128 KiB CHR ROM");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0x2000U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 106 requires 8 KiB volatile PRG RAM and no other RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 106 requires H/V startup mirroring");
    return {};
}

Byte Mapper106::read_prg(Address addr) {
    if (addr >= 0x8000U) {
        const auto bank = prg_banks_[(addr >> 13U) & 3U];
        return prg_rom_[static_cast<std::size_t>(bank) * 0x2000U + (addr & 0x1FFFU)];
    }
    if (addr >= 0x6000U)
        return wram_[addr & 0x1FFFU];
    return 0U;
}

void Mapper106::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        wram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    const auto reg = static_cast<unsigned>(addr & 0x0FU);
    if (reg < 8U) {
        chr_banks_[reg] = reg < 4U ? static_cast<Byte>((value & 0x7EU) | (reg & 1U))
                                   : static_cast<Byte>(value & 0x7FU);
    } else if (reg < 12U) {
        prg_banks_[reg - 8U] = reg == 8U || reg == 11U ? static_cast<Byte>(0x10U | (value & 0x0FU))
                                                       : static_cast<Byte>(value & 0x1FU);
    } else if (reg == 12U) {
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    } else if (reg == 13U) {
        irq_counter_ = 0U;
        irq_enabled_ = false;
    } else if (reg == 14U) {
        irq_counter_ = (irq_counter_ & 0xFF00U) | value;
    } else {
        irq_counter_ = (irq_counter_ & 0x00FFU) | (static_cast<uint16_t>(value) << 8U);
        irq_enabled_ = true;
    }
}

bool Mapper106::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper106::read_chr(Address addr) {
    const auto bank = chr_banks_[(addr >> 10U) & 7U];
    return chr_rom_[static_cast<std::size_t>(bank) * 0x400U + (addr & 0x3FFU)];
}

void Mapper106::write_chr(Address, Byte) {}

MirrorMode Mapper106::mirror_mode() const {
    return mirror_;
}

void Mapper106::power_on() {
    prg_banks_.fill(0x1FU);
    chr_banks_.fill(0x7FU);
    chr_banks_[0] = 0x7EU;
    chr_banks_[2] = 0x7EU;
    mirror_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_enabled_ = false;
    wram_.fill(0U);
}

void Mapper106::reset() {}

bool Mapper106::irq_pending() const {
    return irq_enabled_ && irq_counter_ == 0xFFFFU;
}

void Mapper106::clock_cpu_falling_edge() {
    if (irq_counter_ != 0xFFFFU)
        ++irq_counter_;
}

uint16_t Mapper106::state_tag() const {
    return static_cast<uint16_t>(0x6A00U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper106::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write_array(chr_banks_);
    writer.write_array(prg_banks_);
    writer.write<Byte>(static_cast<Byte>(mirror_));
    writer.write(irq_counter_);
    writer.write<Byte>(irq_enabled_ ? 1U : 0U);
    writer.write_array(wram_);
}

void Mapper106::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<Byte, 8> chr{};
    std::array<Byte, 4> prg{};
    std::array<Byte, 0x2000> wram{};
    reader.read_array(chr);
    reader.read_array(prg);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<uint16_t>();
    const auto enabled = reader.read<Byte>();
    reader.read_array(wram);

    const bool valid_chr =
        std::all_of(chr.begin(), chr.end(), [](Byte bank) { return bank <= 0x7FU; }) &&
        (chr[0] & 1U) == 0U && (chr[1] & 1U) == 1U && (chr[2] & 1U) == 0U && (chr[3] & 1U) == 1U;
    const bool valid_prg = prg[0] >= 0x10U && prg[0] <= 0x1FU && prg[1] <= 0x1FU &&
                           prg[2] <= 0x1FU && prg[3] >= 0x10U && prg[3] <= 0x1FU;
    if (!reader.ok() || tag != state_tag() || !valid_chr || !valid_prg ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || enabled > 1U) {
        reader.fail();
        return;
    }

    chr_banks_ = chr;
    prg_banks_ = prg;
    mirror_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_enabled_ = enabled != 0U;
    wram_ = wram;
}

} // namespace mapperbus::core
