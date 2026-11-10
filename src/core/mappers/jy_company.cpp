#include "core/mappers/jy_company.hpp"

#include <algorithm>
#include <bit>

#include "core/state/state.hpp"

namespace mapperbus::core {
// Original implementation from NESdev J.Y. Company ASIC, revision 24184.
// Unknown $C007 mode and multiplier intermediate values lack hardware evidence;
// retain the last completed product during the documented eight-cycle latency.
JyCompany::JyCompany(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_(std::move(chr)),
      prg_ram_(h.is_nes2 ? h.prg_ram_size + h.prg_nvram_size
                         : (h.mapper_number == 35U || h.has_battery ? 8192U : 0U)),
      chr_ram_(chr_.empty()), persistent_(h.has_battery),
      inhibit_nametables_(h.mapper_number == 90U) {
    if (chr_ram_)
        chr_.resize(h.is_nes2 ? h.chr_ram_size : 8192U);
}
Result<void> JyCompany::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Unsupported J.Y. Company submapper");
    if (prg < 8192U || prg > 2097152U || prg % 8192U != 0U || chr > 2097152U || chr % 1024U != 0U)
        return std::unexpected("Unsupported J.Y. Company ROM geometry");
    if (h.mirror_mode == MirrorMode::FourScreen || h.chr_nvram_size != 0U ||
        (chr != 0U && h.chr_ram_size != 0U) ||
        (h.is_nes2 && chr == 0U &&
         (h.chr_ram_size < 1024U || h.chr_ram_size > 2097152U ||
          !std::has_single_bit(h.chr_ram_size))))
        return std::unexpected("Unsupported J.Y. Company CHR memory geometry");
    if (h.is_nes2 && ((h.prg_ram_size != 0U && h.prg_ram_size != 8192U) ||
                      (h.prg_nvram_size != 0U && h.prg_nvram_size != 8192U) ||
                      (h.prg_ram_size != 0U && h.prg_nvram_size != 0U) ||
                      h.has_battery != (h.prg_nvram_size != 0U)))
        return std::unexpected(
            "J.Y. Company supports absent or 8 KiB WRAM, with matching battery metadata");
    return {};
}
std::size_t JyCompany::prg_address(Address addr) const {
    const unsigned mode = config_[0] & 3U;
    const unsigned shift = mode < 2U ? 2U - mode : 0U;
    const unsigned unit = 8192U << shift;
    unsigned bank = 0U;
    unsigned offset = addr & (unit - 1U);
    if (addr < 0x8000U) {
        bank = (static_cast<unsigned>(prg_banks_[3]) << shift) | ((1U << shift) - 1U);
        offset = addr & 0x1FFFU;
    } else {
        const unsigned slot = (addr - 0x8000U) / unit;
        const unsigned reg = mode == 0U ? 3U : (mode == 1U ? slot * 2U + 1U : slot);
        const bool fixed = reg == 3U && (config_[0] & 4U) == 0U;
        bank = fixed ? 0x7FU : prg_banks_[reg];
    }
    if (mode == 3U) {
        unsigned reversed = 0U;
        for (unsigned bit = 0U; bit < 7U; ++bit)
            reversed |= ((bank >> bit) & 1U) << (6U - bit);
        bank = reversed;
    }
    const unsigned bank_unit = addr < 0x8000U ? 8192U : unit;
    const unsigned inner = (bank * bank_unit + offset) & 0x7FFFFU;
    const unsigned outer = ((static_cast<unsigned>(config_[3]) >> 1U) & 3U) << 19U;
    return (outer | inner) % prg_rom_.size();
}
bool JyCompany::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && ((config_[0] & 0x80U) != 0U || !prg_ram_.empty()));
}
Byte JyCompany::read_prg(Address addr) {
    if (addr >= 0x8000U || (addr >= 0x6000U && (config_[0] & 0x80U) != 0U))
        return prg_rom_[prg_address(addr)];
    return addr >= 0x6000U && !prg_ram_.empty() ? prg_ram_[addr & 0x1FFFU] : 0U;
}
void JyCompany::disable_irq() {
    enabled_ = 0U;
    pending_ = 0U;
    prescaler_ = 0U;
}
void JyCompany::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (addr >= 0x6000U && !prg_ram_.empty() && (config_[0] & 0x80U) == 0U)
            prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    if ((addr & 0xF000U) == 0xC000U) {
        switch (addr & 7U) {
        case 0U:
            if ((value & 1U) == 0U)
                disable_irq();
            else
                enabled_ = 1U;
            break;
        case 1U:
            irq_mode_ = value;
            break;
        case 2U:
            disable_irq();
            break;
        case 3U:
            enabled_ = 1U;
            break;
        case 4U:
            prescaler_ = value ^ xor_;
            break;
        case 5U:
            counter_ = value ^ xor_;
            break;
        case 6U:
            xor_ = value;
            break;
        case 7U:
            irq_config_ = value;
            break;
        default:
            break;
        }
        return;
    }
    if ((addr & 0x800U) != 0U)
        return;
    const unsigned reg = addr & 7U;
    switch (addr & 0xF000U) {
    case 0x8000U:
        prg_banks_[reg & 3U] = value & 0x7FU;
        break;
    case 0x9000U:
        chr_banks_[reg] = (chr_banks_[reg] & 0xFF00U) | value;
        break;
    case 0xA000U:
        chr_banks_[reg] =
            static_cast<uint16_t>((chr_banks_[reg] & 0xFFU) | (static_cast<unsigned>(value) << 8U));
        break;
    case 0xB000U: {
        auto& bank = nt_banks_[reg & 3U];
        bank = reg < 4U
                   ? static_cast<uint16_t>((bank & 0xFF00U) | value)
                   : static_cast<uint16_t>((bank & 0xFFU) | (static_cast<unsigned>(value) << 8U));
        break;
    }
    case 0xD000U:
        config_[reg & 3U] = value;
        break;
    default:
        break;
    }
}
std::size_t JyCompany::chr_address(Address addr) const {
    const unsigned mode = (static_cast<unsigned>(config_[0]) >> 3U) & 3U;
    const unsigned unit = 8192U >> mode;
    const unsigned slot = (addr & 0x1FFFU) / unit;
    unsigned reg = slot << (3U - mode);
    if (mode == 1U && (config_[3] & 0x80U) != 0U)
        reg += 2U * latches_[slot];
    const unsigned mask = (config_[3] & 0x20U) != 0U ? 0x7FFFFU : 0x3FFFFU;
    unsigned outer = ((static_cast<unsigned>(config_[3]) >> 3U) & 3U) << 19U;
    if ((config_[3] & 0x20U) == 0U)
        outer |= (config_[3] & 1U) << 18U;
    return (outer |
            ((static_cast<unsigned>(chr_banks_[reg]) * unit + (addr & (unit - 1U))) & mask)) %
           chr_.size();
}
Byte JyCompany::read_chr(Address addr) {
    // Latches observe physical /RD in observe_ppu_read, not cached pixel reads.
    return chr_[chr_address(addr)];
}
void JyCompany::write_chr(Address addr, Byte value) {
    if (chr_ram_ && (config_[2] & 0x40U) != 0U)
        chr_[chr_address(addr)] = value;
}
MirrorMode JyCompany::mirror_mode() const {
    // Follow the electrical A10/A11 wiring in NESdev's arrangement table.
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    return kModes[config_[1] & 3U];
}
bool JyCompany::chr_nametable(Address addr) const {
    if (inhibit_nametables_ || (config_[0] & 0x20U) == 0U)
        return false;
    return (config_[0] & 0x40U) != 0U ||
           ((static_cast<unsigned>(nt_banks_[(static_cast<unsigned>(addr) >> 10U) & 3U]) ^
             config_[2]) &
            0x80U) != 0U;
}
std::optional<Address> JyCompany::nametable_address(Address addr) const {
    if (inhibit_nametables_ || ((config_[0] & 0x20U) == 0U && (config_[1] & 8U) == 0U))
        return std::nullopt;
    return static_cast<Address>(
        ((nt_banks_[(static_cast<unsigned>(addr) >> 10U) & 3U] & 1U) << 10U) | (addr & 0x3FFU));
}
std::optional<Byte> JyCompany::read_nametable(Address addr) const {
    if (!chr_nametable(addr))
        return std::nullopt;
    // B registers are direct 1 KiB CHR indices, not the masked 900x/A00x path.
    return chr_[(nt_banks_[(static_cast<unsigned>(addr) >> 10U) & 3U] * 1024U + (addr & 0x3FFU)) %
                chr_.size()];
}
bool JyCompany::write_nametable(Address addr, Byte value) {
    if (!chr_nametable(addr))
        return false;
    if (chr_ram_ && (config_[2] & 0x40U) != 0U)
        chr_[(nt_banks_[(static_cast<unsigned>(addr) >> 10U) & 3U] * 1024U + (addr & 0x3FFU)) %
             chr_.size()] = value;
    return true;
}
bool JyCompany::maps_expansion(Address addr) const {
    return addr == 0x5000U || addr == 0x5400U || addr == 0x5C00U || (addr & 0xF800U) == 0x5800U;
}
Byte JyCompany::expansion_read_mask(Address addr) const {
    return (addr & 0xF800U) == 0x5800U && addr != 0x5C00U ? 0xFFU : 0xC0U;
}
Byte JyCompany::read_expansion(Address addr) {
    if (addr == 0x5000U || addr == 0x5400U || addr == 0x5C00U)
        return static_cast<Byte>(jumpers_ << 6U);
    switch (addr & 3U) {
    case 0U:
        return static_cast<Byte>(product_);
    case 1U:
        return static_cast<Byte>(product_ >> 8U);
    case 2U:
        return accumulator_;
    default:
        return test_;
    }
}
void JyCompany::write_expansion(Address addr, Byte value) {
    if ((addr & 0xF800U) != 0x5800U)
        return;
    switch (addr & 3U) {
    case 0U:
        multiplicand_ = value;
        break;
    case 1U:
        multiplier_ = value;
        next_product_ = static_cast<uint16_t>(multiplicand_ * multiplier_);
        multiply_remaining_ = 8U;
        break;
    case 2U:
        accumulator_ = static_cast<Byte>(accumulator_ + value);
        break;
    case 3U:
        accumulator_ = 0U;
        test_ = value;
        break;
    default:
        break;
    }
}
Result<void> JyCompany::configure_board_jumpers(Byte value) {
    if (value > 3U)
        return std::unexpected("J.Y. Company board jumpers must be 0..3");
    jumpers_ = value;
    return {};
}
void JyCompany::reset() {} // Deterministic cold start; preserve cartridge state on console reset.
void JyCompany::clock_irq(Byte source) {
    const unsigned direction = irq_mode_ >> 6U;
    if (enabled_ == 0U || (irq_mode_ & 3U) != source || (direction != 1U && direction != 2U))
        return;
    const Byte mask = (irq_mode_ & 4U) != 0U ? 7U : 0xFFU;
    prescaler_ = static_cast<Byte>(prescaler_ + (direction == 1U ? 1U : 255U));
    if ((prescaler_ & mask) != (direction == 1U ? 0U : mask))
        return;
    counter_ = static_cast<Byte>(counter_ + (direction == 1U ? 1U : 255U));
    if (counter_ == (direction == 1U ? 0U : 0xFFU))
        pending_ = 1U;
}
void JyCompany::clock_cpu() {
    if (multiply_remaining_ != 0U) {
        --multiply_remaining_;
        if (multiply_remaining_ == 0U)
            product_ = next_product_;
    }
    clock_irq(0U);
}
void JyCompany::observe_cpu_write(Address /*addr*/, Byte /*data*/) {
    // Bus strobes are observed before register decoding; C002 then acknowledges
    // even an IRQ raised by that very write. C003 starts with the next strobe.
    clock_irq(3U);
}
bool JyCompany::observes_ppu_a12() const {
    return true;
}
void JyCompany::observe_ppu_a12(bool high) {
    if (high && a12_ == 0U)
        clock_irq(1U);
    a12_ = high ? 1U : 0U;
}
bool JyCompany::observes_ppu_reads() const {
    return true;
}
void JyCompany::observe_ppu_read(Address addr) {
    observe_ppu_a12((addr & 0x1000U) != 0U);
    clock_irq(2U);
    if ((config_[3] & 0x80U) != 0U && addr < 0x2000U) {
        if ((addr & 0xFF8U) == 0xFD8U)
            latches_[addr >> 12U] = 0U;
        else if ((addr & 0xFF8U) == 0xFE8U)
            latches_[addr >> 12U] = 1U;
    }
}
bool JyCompany::irq_pending() const {
    return pending_ != 0U;
}
std::span<const Byte> JyCompany::battery_ram() const {
    return persistent_ ? std::span<const Byte>{prg_ram_} : std::span<const Byte>{};
}
void JyCompany::set_battery_ram(std::span<const Byte> data) {
    if (persistent_ && data.size() == prg_ram_.size())
        std::ranges::copy(data, prg_ram_.begin());
}
void JyCompany::save_state(StateWriter& w) const {
    w.write_array(prg_banks_);
    w.write_array(chr_banks_);
    w.write_array(nt_banks_);
    w.write_array(config_);
    w.write_array(latches_);
    w.write(jumpers_);
    w.write(multiplicand_);
    w.write(multiplier_);
    w.write(product_);
    w.write(next_product_);
    w.write(multiply_remaining_);
    w.write(accumulator_);
    w.write(test_);
    w.write(irq_mode_);
    w.write(irq_config_);
    w.write(prescaler_);
    w.write(counter_);
    w.write(xor_);
    w.write(enabled_);
    w.write(pending_);
    w.write(a12_);
    w.write_bytes(prg_ram_);
    if (chr_ram_)
        w.write_bytes(chr_);
}
void JyCompany::load_state(StateReader& r) {
    const auto prg_size = prg_ram_.size();
    const auto chr_size = chr_.size();
    r.read_array(prg_banks_);
    r.read_array(chr_banks_);
    r.read_array(nt_banks_);
    r.read_array(config_);
    r.read_array(latches_);
    r.read(jumpers_);
    r.read(multiplicand_);
    r.read(multiplier_);
    r.read(product_);
    r.read(next_product_);
    r.read(multiply_remaining_);
    r.read(accumulator_);
    r.read(test_);
    r.read(irq_mode_);
    r.read(irq_config_);
    r.read(prescaler_);
    r.read(counter_);
    r.read(xor_);
    r.read(enabled_);
    r.read(pending_);
    r.read(a12_);
    r.read_bytes(prg_ram_, prg_size);
    if (chr_ram_)
        r.read_bytes(chr_, chr_size);
    if (!r.ok() || prg_ram_.size() != prg_size || chr_.size() != chr_size || jumpers_ > 3U ||
        multiply_remaining_ > 8U || enabled_ > 1U || pending_ > 1U || a12_ > 1U ||
        latches_[0] > 1U || latches_[1] > 1U ||
        std::ranges::any_of(prg_banks_, [](Byte bank) { return bank > 0x7FU; })) {
        // Keep even a failed component load safe until machine-level rollback.
        prg_ram_.resize(prg_size);
        chr_.resize(chr_size);
        prg_banks_ = {};
        latches_ = {};
        jumpers_ = 0U;
        multiply_remaining_ = 0U;
        r.fail();
    }
}
} // namespace mapperbus::core
