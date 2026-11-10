#include "core/mappers/datach.hpp"

#include <algorithm>
#include <string>

namespace mapperbus::core {

Datach::Datach(const INesHeader& h, std::vector<Byte> prg, const std::vector<Byte>& /*chr*/)
    : prg_rom_(std::move(prg)), external_(h.is_nes2 ? h.prg_nvram_size == 128U : h.has_battery),
      external_eeprom_(external_ ? EepromKind::X24C01 : EepromKind::None) {
    update_serial();
}

Result<void> Datach::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if ((h.is_nes2 && h.submapper != 0U) || prg < 0x8000U || prg > 0x40000U ||
        prg % 0x4000U != 0U || chr != 0U)
        return std::unexpected("Unsupported Datach ROM geometry or submapper");
    if (h.chr_nvram_size != 0U || (h.chr_ram_size != 0U && h.chr_ram_size != 8192U) ||
        (h.is_nes2 &&
         (h.prg_ram_size != 0U || (h.prg_nvram_size != 0U && h.prg_nvram_size != 128U) ||
          h.has_battery != (h.prg_nvram_size != 0U))))
        return std::unexpected(
            "Datach header describes only the optional 128-byte cartridge EEPROM");
    if (h.mirror_mode != MirrorMode::Vertical && h.mirror_mode != MirrorMode::Horizontal)
        return std::unexpected("Datach does not provide four-screen VRAM");
    return {};
}

bool Datach::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}
Byte Datach::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : ((serial_control_ & 0x80U) != 0U ? 0x18U : 0U);
}
Byte Datach::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;
    if (addr < 0x8000U) {
        // Two open-drain EEPROM outputs share D4; the scanner drives D3 only
        // in read mode (BA-BAR's paired 74HC368 gates).
        const bool white = barcode_size_ == 0U || barcode_[barcode_position_] == 0U;
        return static_cast<Byte>((bus_sda_ != 0U ? 0x10U : 0U) | (white ? 8U : 0U));
    }
    const auto bank = addr < 0xC000U
                          ? static_cast<std::size_t>(prg_bank_) % (prg_rom_.size() / 0x4000U)
                          : prg_rom_.size() / 0x4000U - 1U;
    return prg_rom_[bank * 0x4000U + (addr & 0x3FFFU)];
}
void Datach::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    const auto reg = addr & 15U;
    if (reg < 4U) {
        clocks_[reg] = (static_cast<unsigned>(value) >> 3U) & 1U;
        update_serial();
    } else if (reg == 8U) {
        prg_bank_ = value & 15U;
    } else if (reg == 9U) {
        mirror_ = value & 3U;
    } else if (reg == 10U) {
        irq_.control(value, true);
    } else if (reg == 11U || reg == 12U) {
        irq_.write(value, reg == 12U, true);
    } else if (reg == 13U) {
        serial_control_ = value & 0xE0U;
        update_serial();
    }
}
void Datach::update_serial() {
    const bool host = (serial_control_ & 0xC0U) != 0U;
    const bool internal_clock = (serial_control_ & 0x20U) != 0U;
    const bool external_clock = clocks_[ppu_slot_] != 0U;
    bool previous = bus_sda_ != 0U;
    // Sample both devices before resolving their outputs. Propagate SDA
    // changes to a peer whose independent clock is held high, too.
    for (unsigned pass = 0U; pass < 4U; ++pass) {
        const bool next =
            host && internal_eeprom_.releases_sda() && external_eeprom_.releases_sda();
        internal_eeprom_.set_bus_lines(internal_clock, host, previous, next);
        external_eeprom_.set_bus_lines(external_clock, host, previous, next);
        const bool resolved =
            host && internal_eeprom_.releases_sda() && external_eeprom_.releases_sda();
        bus_sda_ = resolved ? 1U : 0U;
        if (resolved == next)
            break;
        previous = next;
    }
}
Byte Datach::read_chr(Address addr) {
    observe_ppu_address(addr);
    return chr_ram_[addr & 0x1FFFU];
}
void Datach::write_chr(Address addr, Byte value) {
    observe_ppu_address(addr);
    chr_ram_[addr & 0x1FFFU] = value;
}
bool Datach::observes_ppu_address() const {
    return external_;
}
void Datach::observe_ppu_address(Address addr) {
    const auto slot = static_cast<Byte>((static_cast<unsigned>(addr) >> 10U) & 3U);
    if (slot != ppu_slot_) {
        ppu_slot_ = slot;
        update_serial();
    }
}
MirrorMode Datach::mirror_mode() const {
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    return kModes[mirror_];
}
void Datach::reset() {
    prg_bank_ = mirror_ = 0U;
    irq_.reset();
    // Retain serial pins, independent EEPROM programming and an ongoing swipe.
}
void Datach::clock_cpu() {
    irq_.clock();
    internal_eeprom_.clock_cpu();
    external_eeprom_.clock_cpu();
    if (barcode_size_ != 0U && ++module_phase_ == module_cycles_) {
        module_phase_ = 0U;
        if (++barcode_position_ == barcode_size_) {
            barcode_size_ = barcode_position_ = 0U;
            barcode_.fill(0U);
        }
    }
}
bool Datach::irq_pending() const {
    return irq_.pending();
}

Result<void> Datach::scan_barcode(std::string_view digits, uint32_t module_cycles) {
    if ((digits.size() != 8U && digits.size() != 12U && digits.size() != 13U) ||
        std::ranges::any_of(digits, [](char c) { return c < '0' || c > '9'; }) ||
        module_cycles == 0U || module_cycles > 1'000'000U)
        return std::unexpected(
            "Datach requires 8, 12 or 13 digits and 1..1000000 M2 cycles per module");
    if (barcode_size_ != 0U)
        return std::unexpected("A Datach barcode scan is already in progress");
    // GS1 General Specifications 9.0, 5.2.2: number sets A/B/C and parity.
    constexpr std::array<Byte, 10> kA = {
        0x0D, 0x19, 0x13, 0x3D, 0x23, 0x31, 0x2F, 0x3B, 0x37, 0x0B};
    constexpr std::array<Byte, 10> kB = {
        0x27, 0x33, 0x1B, 0x21, 0x1D, 0x39, 0x05, 0x11, 0x09, 0x17};
    constexpr std::array<Byte, 10> kParity = {
        0, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A};
    const std::string code = digits.size() == 12U ? "0" + std::string(digits) : std::string(digits);
    barcode_.fill(0U);
    unsigned count = 11U; // White quiet zones, at least the GS1 minimum.
    const auto append = [this, &count](unsigned pattern, unsigned bits) {
        while (bits != 0U)
            barcode_[count++] = static_cast<Byte>((pattern >> --bits) & 1U);
    };
    append(5U, 3U);
    const bool short_code = code.size() == 8U;
    const unsigned half = short_code ? 4U : 6U;
    for (unsigned pos = 0U; pos < half; ++pos) {
        const auto digit = static_cast<unsigned>(code[pos + (short_code ? 0U : 1U)] - '0');
        const bool set_b = !short_code && (kParity[static_cast<unsigned>(code[0] - '0')] &
                                           (1U << (5U - pos))) != 0U;
        append(set_b ? kB[digit] : kA[digit], 7U);
    }
    append(10U, 5U);
    for (unsigned pos = code.size() - half; pos < code.size(); ++pos)
        append(kA[static_cast<unsigned>(code[pos] - '0')] ^ 0x7FU, 7U);
    append(5U, 3U);
    barcode_size_ = static_cast<Byte>(count + 11U);
    barcode_position_ = 0U;
    module_cycles_ = module_cycles;
    module_phase_ = 0U;
    return {};
}
std::span<const Byte> Datach::shared_nvram() const {
    return internal_eeprom_.nvram();
}
void Datach::set_shared_nvram(std::span<const Byte> data) {
    internal_eeprom_.restore_nvram(data);
}
std::span<const Byte> Datach::battery_ram() const {
    return external_eeprom_.nvram();
}
void Datach::set_battery_ram(std::span<const Byte> data) {
    external_eeprom_.restore_nvram(data);
}
void Datach::save_state(StateWriter& w) const {
    w.write_array(chr_ram_);
    w.write_array(clocks_);
    w.write(prg_bank_);
    w.write(mirror_);
    w.write(ppu_slot_);
    w.write(serial_control_);
    w.write(bus_sda_);
    irq_.save_state(w);
    internal_eeprom_.save_state(w);
    external_eeprom_.save_state(w);
    w.write_array(barcode_);
    w.write(barcode_size_);
    w.write(barcode_position_);
    w.write(module_cycles_);
    w.write(module_phase_);
}
void Datach::load_state(StateReader& r) {
    r.read_array(chr_ram_);
    r.read_array(clocks_);
    r.read(prg_bank_);
    r.read(mirror_);
    r.read(ppu_slot_);
    r.read(serial_control_);
    r.read(bus_sda_);
    irq_.load_state(r);
    internal_eeprom_.load_state(r);
    external_eeprom_.load_state(r);
    r.read_array(barcode_);
    r.read(barcode_size_);
    r.read(barcode_position_);
    r.read(module_cycles_);
    r.read(module_phase_);
    if (!r.ok() || prg_bank_ > 15U || mirror_ > 3U || ppu_slot_ > 3U ||
        (serial_control_ & 0x1FU) != 0U || bus_sda_ > 1U ||
        std::ranges::any_of(clocks_, [](Byte b) { return b > 1U; }) ||
        std::ranges::any_of(barcode_, [](Byte b) { return b > 1U; }) ||
        (barcode_size_ != 0U && barcode_size_ != 89U && barcode_size_ != 117U) ||
        (barcode_size_ == 0U ? barcode_position_ != 0U || module_phase_ != 0U
                             : barcode_position_ >= barcode_size_) ||
        module_cycles_ == 0U || module_cycles_ > 1'000'000U || module_phase_ >= module_cycles_ ||
        (bus_sda_ != 0U) != ((serial_control_ & 0xC0U) != 0U && internal_eeprom_.releases_sda() &&
                             external_eeprom_.releases_sda())) {
        barcode_size_ = barcode_position_ = 0U;
        module_phase_ = 0U;
        module_cycles_ = 1000U;
        clocks_.fill(0U);
        ppu_slot_ = 0U;
        serial_control_ = 0x60U;
        bus_sda_ = 1U;
        reset();
        r.fail();
    }
}

} // namespace mapperbus::core
