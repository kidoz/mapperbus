#include "core/mappers/flash_boards.hpp"

#include <bit>

#include "core/state/state.hpp"

namespace mapperbus::core {
FlashBoards::FlashBoards(const INesHeader& h,
                         std::vector<Byte> prg,
                         const std::vector<Byte>& /*chr*/)
    : rom_(std::move(prg)),
      chr_ram_(h.mapper_number == 111U
                   ? 32768U
                   : (h.is_nes2 && h.chr_ram_size != 0U ? h.chr_ram_size : 32768U)),
      gtrom_(h.mapper_number == 111U), submapper_(h.is_nes2 ? h.submapper : 0U),
      conflicts_(!gtrom_ && (submapper_ == 2U || (submapper_ == 0U && !h.has_battery))),
      single_screen_(!gtrom_ && submapper_ != 3U && h.mirror_mode == MirrorMode::FourScreen &&
                     (h.nametable_bits & 1U) == 0U),
      four_screen_(!gtrom_ && submapper_ != 3U && h.mirror_mode == MirrorMode::FourScreen &&
                   (h.nametable_bits & 1U) != 0U),
      fixed_mirror_(h.mirror_mode) {
    if (gtrom_ || h.has_battery)
        flash_.emplace(std::move(rom_));
}
Result<void> FlashBoards::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    const bool gt = h.mapper_number == 111U;
    if (h.is_nes2 && h.submapper > (gt ? 0U : 4U))
        return std::unexpected("Unsupported flash-board submapper");
    if (prg < 32768U || prg > 524288U || !std::has_single_bit(prg))
        return std::unexpected("Flash board requires a power-of-two 32..512 KiB PRG image");
    if (gt && chr != 0U) {
        if (prg > 262144U || chr > 262144U || chr < 4096U || !std::has_single_bit(chr) ||
            h.chr_ram_size != 0U || h.chr_nvram_size != 0U ||
            h.mirror_mode == MirrorMode::FourScreen)
            return std::unexpected("Unsupported mapper-111 direct MMC1 geometry");
        if (h.is_nes2 &&
            (h.prg_ram_size + h.prg_nvram_size != 0U && h.prg_ram_size + h.prg_nvram_size != 8192U))
            return std::unexpected("Mapper-111 direct MMC1 supports absent or 8 KiB PRG RAM");
        if (h.is_nes2 && (h.prg_ram_size > 8192U || h.prg_nvram_size > 8192U ||
                          (h.prg_ram_size != 0U && h.prg_nvram_size != 0U) ||
                          h.has_battery != (h.prg_nvram_size != 0U)))
            return std::unexpected("Mapper-111 direct MMC1 RAM metadata disagrees");
        return {};
    }
    if (chr != 0U || h.chr_nvram_size != 0U ||
        (h.is_nes2 && (h.prg_nvram_size != 0U || h.prg_ram_size != 0U)) ||
        (h.is_nes2 && h.chr_ram_size != 0U &&
         (gt ? h.chr_ram_size != 32768U
             : h.chr_ram_size != 8192U && h.chr_ram_size != 16384U && h.chr_ram_size != 32768U)))
        return std::unexpected("Unsupported flash-board memory geometry");
    if (!gt && h.is_nes2 && (h.submapper == 2U || h.submapper == 3U) && h.has_battery)
        return std::unexpected("This UNROM 512 submapper does not support flash saves");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical &&
        h.mirror_mode != MirrorMode::FourScreen)
        return std::unexpected("Unsupported flash-board nametable metadata");
    return {};
}
bool FlashBoards::gtrom_alias(Address addr) {
    return (addr & 0xD000U) == 0x5000U;
}
uint32_t FlashBoards::prg_address(Address addr) const {
    if (gtrom_)
        return static_cast<uint32_t>(latch_ & 15U) * 32768U + (addr & 0x7FFFU);
    const auto banks = flash_ ? 32U : static_cast<uint32_t>(rom_.size() / 16384U);
    return static_cast<uint32_t>(addr < 0xC000U ? (latch_ & 31U) % banks : banks - 1U) * 16384U +
           (addr & 0x3FFFU);
}
bool FlashBoards::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}
Byte FlashBoards::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto offset = prg_address(addr);
    return flash_ ? flash_->read(offset) : rom_[offset];
}
void FlashBoards::write_prg(Address addr, Byte value) {
    if (gtrom_ && gtrom_alias(addr)) {
        latch_ = value;
        return;
    }
    if (addr < 0x8000U)
        return;
    if (gtrom_) {
        if (flash_)
            flash_->write(prg_address(addr), value);
        return;
    }
    if (addr >= 0xC000U || (submapper_ == 0U && !flash_) || submapper_ == 2U) {
        latch_ = conflicts_ ? static_cast<Byte>(value & read_prg(addr)) : value;
    } else {
        if (submapper_ == 4U)
            leds_ = value;
        if (flash_)
            flash_->write(prg_address(addr), value);
    }
}
std::size_t FlashBoards::pattern_address(Address addr) const {
    const unsigned bank = gtrom_ ? (static_cast<unsigned>(latch_) >> 4U) & 1U
                                 : (static_cast<unsigned>(latch_) >> 5U) & 3U;
    return (bank * 8192U + (addr & 0x1FFFU)) % (gtrom_ ? 16384U : chr_ram_.size());
}
Byte FlashBoards::read_chr(Address addr) {
    return chr_ram_[pattern_address(addr)];
}
void FlashBoards::write_chr(Address addr, Byte value) {
    chr_ram_[pattern_address(addr)] = value;
}
MirrorMode FlashBoards::mirror_mode() const {
    if (gtrom_ || four_screen_)
        return MirrorMode::FourScreen;
    if (submapper_ == 3U)
        return (latch_ & 0x80U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    if (single_screen_)
        return (latch_ & 0x80U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    return fixed_mirror_;
}
void FlashBoards::reset() {} // Latches and flash have no console reset input.
void FlashBoards::clock_cpu() {
    if (flash_)
        flash_->clock_cpu();
}
bool FlashBoards::maps_expansion(Address /*addr*/) const {
    return false;
}
Byte FlashBoards::expansion_read_mask(Address /*addr*/) const {
    return 0U;
}
void FlashBoards::write_expansion(Address addr, Byte value) {
    if (gtrom_ && gtrom_alias(addr))
        latch_ = value;
}
void FlashBoards::observe_cpu_read(Address addr, Byte data) {
    if (gtrom_ && gtrom_alias(addr))
        latch_ = data;
}
bool FlashBoards::unmirrored_nametables() const {
    return gtrom_ || four_screen_;
}
std::optional<Byte> FlashBoards::read_nametable(Address addr) const {
    if (!unmirrored_nametables())
        return std::nullopt;
    const auto base =
        gtrom_ ? 16384U + ((latch_ & 0x20U) != 0U ? 8192U : 0U) : chr_ram_.size() - 8192U;
    return chr_ram_[base + (addr & 0x1FFFU)];
}
bool FlashBoards::write_nametable(Address addr, Byte value) {
    if (!unmirrored_nametables())
        return false;
    const auto base =
        gtrom_ ? 16384U + ((latch_ & 0x20U) != 0U ? 8192U : 0U) : chr_ram_.size() - 8192U;
    chr_ram_[base + (addr & 0x1FFFU)] = value;
    return true;
}
bool FlashBoards::has_flash_storage() const {
    return flash_.has_value();
}
std::span<const Byte> FlashBoards::battery_ram() const {
    return flash_ ? flash_->nvram() : std::span<const Byte>{};
}
void FlashBoards::set_battery_ram(std::span<const Byte> data) {
    if (flash_)
        flash_->restore_nvram(data);
}
std::optional<Byte> FlashBoards::led_outputs() const {
    if (gtrom_)
        return static_cast<Byte>((static_cast<unsigned>(~latch_) >> 6U) & 3U);
    if (submapper_ == 4U)
        return static_cast<Byte>(~leds_);
    return std::nullopt;
}
void FlashBoards::save_state(StateWriter& w) const {
    w.write(latch_);
    w.write(leds_);
    w.write_bytes(chr_ram_);
    if (flash_)
        flash_->save_state(w);
}
void FlashBoards::load_state(StateReader& r) {
    const auto chr_size = chr_ram_.size();
    r.read(latch_);
    r.read(leds_);
    r.read_bytes(chr_ram_, 32768U);
    if (flash_)
        flash_->load_state(r);
    if (!r.ok() || chr_ram_.size() != chr_size ||
        ((gtrom_ || submapper_ != 4U) && leds_ != 0xFFU)) {
        chr_ram_.resize(chr_size);
        latch_ = 0U;
        leds_ = 0xFFU;
        r.fail();
    }
}
} // namespace mapperbus::core
