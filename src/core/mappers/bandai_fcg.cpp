#include "core/mappers/bandai_fcg.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
bool uses_wram(const INesHeader& h) {
    return h.mapper_number == 153U || (h.mapper_number == 16U && h.is_nes2 && h.submapper == 3U);
}
EepromKind eeprom_kind(const INesHeader& h) {
    if (uses_wram(h) || (h.mapper_number == 16U && h.is_nes2 && h.submapper == 4U))
        return EepromKind::None;
    if (h.mapper_number == 159U || (h.is_nes2 && h.submapper == 1U))
        return EepromKind::X24C01;
    return (h.is_nes2 ? h.prg_nvram_size != 0U : h.has_battery) ? EepromKind::X24C02
                                                                : EepromKind::None;
}
} // namespace

// NESdev mapper 16 r3876, 153 r5112, 159 r5171; Xicor data sheets.
BandaiFcg::BandaiFcg(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), wram_(uses_wram(h)),
      low_only_(h.mapper_number == 16U && h.is_nes2 && h.submapper == 4U),
      dual_decode_(h.mapper_number == 16U && (!h.is_nes2 || h.submapper == 0U)),
      eeprom_kind_(eeprom_kind(h)), eeprom_(eeprom_kind_) {
    // Deterministic fresh NVRAM, not a measurement of SRAM power-up values.
    prg_ram_.fill(0xFFU);
    reset();
}

Result<void> BandaiFcg::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.is_nes2 && ((h.mapper_number == 16U && h.submapper != 0U && h.submapper != 1U &&
                       h.submapper != 3U && h.submapper != 4U && h.submapper != 5U) ||
                      (h.mapper_number != 16U && h.submapper != 0U)))
        return std::unexpected("Unsupported Bandai submapper (Datach requires mapper 157)");
    const bool ram = uses_wram(h);
    if (prg < 0x8000U || prg > (ram ? 0x80000U : 0x40000U) || prg % 0x4000U != 0U ||
        (ram ? chr != 0U : (chr == 0U || chr > 0x40000U || chr % 0x400U != 0U)))
        return std::unexpected("Unsupported Bandai ROM geometry");
    if (h.chr_nvram_size != 0U || (h.chr_ram_size != 0U && (!ram || h.chr_ram_size != 0x2000U)) ||
        (h.is_nes2 && h.prg_ram_size != 0U))
        return std::unexpected("Unsupported Bandai RAM geometry");
    if (h.is_nes2) {
        const auto kind = eeprom_kind(h);
        const std::size_t expected = ram                          ? 8192U
                                     : kind == EepromKind::X24C01 ? 128U
                                     : kind == EepromKind::X24C02 ? 256U
                                                                  : 0U;
        if (h.prg_nvram_size != expected || h.has_battery != (expected != 0U))
            return std::unexpected("Bandai NVRAM size and battery metadata disagree");
    }
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Bandai boards do not provide four-screen VRAM");
    return {};
}

void BandaiFcg::reset() {
    chr_banks_.fill(0U);
    prg_bank_ = 0U;
    mirror_ = 0U;
    ppu_slot_ = 0U;
    ram_enabled_ = 0U;
    irq_.reset();
    // Retain RAM and the independently powered EEPROM, including a write
    // already in progress. Startup defaults are emulator policy.
}

bool BandaiFcg::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && (wram_ ? ram_enabled_ != 0U
                                                         : eeprom_kind_ != EepromKind::None &&
                                                               (serial_control_ & 0x80U) != 0U));
}

Byte BandaiFcg::prg_read_mask(Address addr) const {
    return addr >= 0x8000U || wram_ ? 0xFFU : 0x10U;
}

Byte BandaiFcg::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;
    if (addr < 0x8000U)
        return wram_ ? (ram_enabled_ != 0U ? prg_ram_[addr & 0x1FFFU] : 0U)
                     : (eeprom_.sda() ? 0x10U : 0U);
    const unsigned outer = wram_ ? (chr_banks_[ppu_slot_] & 1U) << 4U : 0U;
    const auto bank = static_cast<std::size_t>(outer | (addr < 0xC000U ? prg_bank_ : 15U));
    return prg_rom_[(bank * 0x4000U + (addr & 0x3FFFU)) % prg_rom_.size()];
}

void BandaiFcg::write_prg(Address addr, Byte value) {
    if (addr < 0x6000U)
        return;
    if (wram_ && addr < 0x8000U) {
        if (ram_enabled_ != 0U)
            prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    const bool low = addr < 0x8000U;
    if ((low && !low_only_ && !dual_decode_) || (!low && low_only_))
        return;
    const auto reg = addr & 15U;
    if (reg < 8U) {
        if (!wram_ || reg < 4U)
            chr_banks_[reg] = wram_ ? value & 1U : value;
    } else if (reg == 8U) {
        prg_bank_ = value & 15U;
    } else if (reg == 9U) {
        mirror_ = value & 3U;
    } else if (reg == 10U) {
        irq_.control(value, !low);
    } else if (reg == 11U || reg == 12U) {
        irq_.write(value, reg == 12U, !low);
    } else if (reg == 13U && !low) {
        if (wram_) {
            ram_enabled_ = (static_cast<unsigned>(value) >> 5U) & 1U;
        } else if (eeprom_kind_ != EepromKind::None) {
            serial_control_ = value & 0xE0U;
            eeprom_.set_lines((value & 0x20U) != 0U, (value & 0xC0U) != 0U);
        }
    }
}

void BandaiFcg::observe_ppu_address(Address addr) {
    if (wram_)
        ppu_slot_ = static_cast<Byte>((static_cast<unsigned>(addr) >> 10U) & 3U);
}
bool BandaiFcg::observes_ppu_address() const {
    return wram_;
}

Byte BandaiFcg::read_chr(Address addr) {
    if (wram_) {
        observe_ppu_address(addr);
        return chr_ram_[addr & 0x1FFFU];
    }
    return chr_rom_
        [(static_cast<std::size_t>(chr_banks_[(static_cast<unsigned>(addr) >> 10U) & 7U]) * 0x400U +
          (addr & 0x3FFU)) %
         chr_rom_.size()];
}
void BandaiFcg::write_chr(Address addr, Byte value) {
    if (wram_) {
        observe_ppu_address(addr);
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode BandaiFcg::mirror_mode() const {
    constexpr std::array<MirrorMode, 4> kModes = {MirrorMode::Vertical,
                                                  MirrorMode::Horizontal,
                                                  MirrorMode::SingleLower,
                                                  MirrorMode::SingleUpper};
    return kModes[mirror_];
}
void BandaiFcg::clock_cpu() {
    eeprom_.clock_cpu();
    irq_.clock();
}
bool BandaiFcg::irq_pending() const {
    return irq_.pending();
}

std::span<const Byte> BandaiFcg::battery_ram() const {
    return wram_ ? std::span<const Byte>(prg_ram_) : eeprom_.nvram();
}
void BandaiFcg::set_battery_ram(std::span<const Byte> data) {
    if (wram_)
        std::copy_n(data.begin(), std::min(data.size(), prg_ram_.size()), prg_ram_.begin());
    else
        eeprom_.restore_nvram(data);
}
void BandaiFcg::save_state(StateWriter& w) const {
    w.write_array(chr_banks_);
    w.write(prg_bank_);
    w.write(mirror_);
    w.write(ppu_slot_);
    w.write(ram_enabled_);
    w.write(serial_control_);
    irq_.save_state(w);
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
    eeprom_.save_state(w);
}
void BandaiFcg::load_state(StateReader& r) {
    r.read_array(chr_banks_);
    r.read(prg_bank_);
    r.read(mirror_);
    r.read(ppu_slot_);
    r.read(ram_enabled_);
    r.read(serial_control_);
    irq_.load_state(r);
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    eeprom_.load_state(r);
    if (!r.ok() || prg_bank_ > 15U || mirror_ > 3U || ppu_slot_ > 3U || ram_enabled_ > 1U ||
        (serial_control_ & 0x1FU) != 0U || (!wram_ && (ram_enabled_ != 0U || ppu_slot_ != 0U)) ||
        (wram_ && std::ranges::any_of(chr_banks_, [](Byte b) { return b > 1U; }))) {
        reset();
        r.fail();
    }
}

} // namespace mapperbus::core
