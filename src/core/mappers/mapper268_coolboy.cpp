#include "core/mappers/mapper268_coolboy.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr uint32_t kStateTag = 0xAC00U;
constexpr std::size_t kWramSize = 8192U;
constexpr std::size_t kMaxChrRamSize = 262144U;

void replace_pin(uint32_t& address, unsigned pin, bool high) {
    const auto mask = uint32_t{1U} << pin;
    address = (address & ~mask) | (high ? mask : 0U);
}

bool pin(uint32_t address, unsigned index) {
    return (address & (uint32_t{1U} << index)) != 0U;
}

} // namespace

// Original implementation from NESdev mapper 268 r24626 and the linked
// AA6023A v1.2 / AA6023B v1.0 datasheets and SMD133 pinout.
Coolboy::Coolboy(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_(header.prg_ram_size + header.prg_nvram_size), chr_ram_(header.chr_ram_size),
      submapper_(header.submapper), initial_mirror_(header.mirror_mode),
      persistent_wram_(header.has_battery) {
    power_on();
}

Result<void> Coolboy::validate_geometry(const INesHeader& h,
                                        std::size_t prg_size,
                                        std::size_t chr_size) {
    if (!h.is_nes2 || h.mapper_number != 268U || h.submapper > 11U)
        return std::unexpected("Mapper 268 requires NES 2.0 submappers 0 through 11");

    std::size_t max_prg = 16777216U;
    if (h.submapper == 4U || h.submapper == 5U)
        max_prg = 4194304U;
    else if (h.submapper == 8U || h.submapper == 9U)
        max_prg = 2097152U;
    else if (h.submapper == 10U || h.submapper == 11U)
        max_prg = 8388608U;
    const std::size_t min_prg = h.submapper == 6U || h.submapper == 7U ? 65536U : 32768U;
    if (!std::has_single_bit(prg_size) || prg_size < min_prg || prg_size > max_prg)
        return std::unexpected("Unsupported mapper-268 PRG geometry for this submapper");

    std::size_t max_chr_rom = 262144U;
    if (h.submapper <= 3U)
        max_chr_rom = 1048576U;
    else if (h.submapper == 4U || h.submapper == 5U)
        max_chr_rom = 4194304U;
    const bool ram_only = h.submapper >= 6U && h.submapper <= 9U;
    if ((ram_only && chr_size != 0U) ||
        (chr_size != 0U &&
         (!std::has_single_bit(chr_size) || chr_size < 8192U || chr_size > max_chr_rom)))
        return std::unexpected("Unsupported mapper-268 CHR-ROM geometry for this submapper");

    const std::size_t max_chr_ram =
        h.submapper == 6U || h.submapper == 7U ? 131072U : kMaxChrRamSize;
    if (h.chr_nvram_size != 0U ||
        (h.chr_ram_size != 0U && (!std::has_single_bit(h.chr_ram_size) || h.chr_ram_size < 2048U ||
                                  h.chr_ram_size > max_chr_ram)))
        return std::unexpected("Unsupported mapper-268 CHR-RAM geometry for this submapper");
    if (chr_size == 0U && h.chr_ram_size == 0U)
        return std::unexpected("Mapper 268 requires CHR ROM or declared volatile CHR RAM");
    if (chr_size != 0U && h.chr_ram_size != 0U && (h.submapper > 3U || h.chr_ram_size != 2048U))
        return std::unexpected(
            "Mixed CHR ROM/RAM is documented only as a 2 KiB submapper-0..3 variant");

    const auto wram = h.prg_ram_size + h.prg_nvram_size;
    if ((wram != 0U && wram != kWramSize) || (h.prg_ram_size != 0U && h.prg_nvram_size != 0U) ||
        h.has_battery != (h.prg_nvram_size == kWramSize))
        return std::unexpected("Mapper 268 supports one optional 8 KiB WRAM/NVRAM chip");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 268 requires H/V startup CIRAM metadata");
    return {};
}

bool Coolboy::submapper_in(unsigned first, unsigned last) const {
    return submapper_ >= first && submapper_ <= last;
}

bool Coolboy::gnrom_mode() const {
    return (state_.outer[3] & 0x20U) != 0U;
}

bool Coolboy::weird_mode() const {
    return (state_.outer[3] & 0x40U) != 0U;
}

bool Coolboy::lockout_active() const {
    return (state_.outer[3] & 0x80U) != 0U && !gnrom_mode();
}

unsigned Coolboy::outer_base() const {
    if ((submapper_ & 1U) != 0U)
        return 0x5000U;
    return submapper_ == 2U ? 0x7000U : 0x6000U;
}

bool Coolboy::is_outer_register(Address addr) const {
    return (addr & 0xF000U) == outer_base();
}

void Coolboy::write_outer(unsigned index, Byte value) {
    if (index != 2U && lockout_active())
        return;
    if (index == 2U && (state_.outer[2] & 0x80U) != 0U) {
        state_.outer[2] = static_cast<Byte>((state_.outer[2] & 0xF0U) | (value & 0x0FU));
        return;
    }
    state_.outer[index] = value;
}

bool Coolboy::extra_wram_mapped(Address addr) const {
    return addr >= 0x5000U && addr < 0x6000U && (state_.outer[3] & 0x10U) != 0U &&
           !prg_ram_.empty();
}

bool Coolboy::base_wram_readable(Address addr) const {
    return addr >= 0x6000U && addr < 0x8000U && state_.wram_enabled && !prg_ram_.empty();
}

void Coolboy::write_ram(Address addr, Byte value) {
    if (state_.wram_write_protected || prg_ram_.empty())
        return;
    if (extra_wram_mapped(addr))
        prg_ram_[addr & 0x0FFFU] = value;
    else if (base_wram_readable(addr))
        prg_ram_[addr & 0x1FFFU] = value;
}

Byte Coolboy::read_prg(Address addr) {
    if (addr < 0x8000U)
        return base_wram_readable(addr) ? prg_ram_[addr & 0x1FFFU] : 0U;
    return prg_rom_[mapped_prg_offset(addr)];
}

void Coolboy::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if ((submapper_ & 1U) == 0U && is_outer_register(addr))
            write_outer(addr & 7U, value);
        write_ram(addr, value);
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        state_.bank_select = value & 0xC7U;
        break;
    case 0x8001U:
        state_.banks[state_.bank_select & 7U] = value;
        break;
    case 0xA000U:
        state_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        state_.wram_enabled = (value & 0x80U) != 0U;
        state_.wram_write_protected = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        state_.irq_reload = value;
        break;
    case 0xC001U:
        state_.irq_counter = 0U;
        state_.irq_reload_flag = true;
        break;
    case 0xE000U:
        state_.irq_enabled = false;
        state_.irq_pending = false;
        break;
    case 0xE001U:
        state_.irq_enabled = true;
        break;
    default:
        break;
    }
}

bool Coolboy::maps_prg(Address addr) const {
    if (addr >= 0x8000U)
        return true;
    return ((submapper_ & 1U) == 0U && is_outer_register(addr)) || base_wram_readable(addr);
}

Byte Coolboy::prg_read_mask(Address addr) const {
    return addr >= 0x8000U || base_wram_readable(addr) ? 0xFFU : 0U;
}

Byte Coolboy::mmc3_chr_bank(Address addr) const {
    const auto slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^
                      ((state_.bank_select & 0x80U) != 0U ? 4U : 0U);
    if (slot >= 4U)
        return state_.banks[slot - 2U];
    if (weird_mode())
        return (slot & 1U) != 0U ? 0U : state_.banks[slot / 2U];
    return static_cast<Byte>((state_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
}

std::size_t Coolboy::mapped_chr_offset(Address addr) const {
    uint32_t offset = 0U;
    if (gnrom_mode()) {
        const unsigned mask = ((((static_cast<unsigned>(state_.outer[2]) >> 4U) & 7U) << 1U) | 1U);
        const unsigned bank = (state_.outer[2] & 0x0FU) & mask;
        offset = (bank << 13U) | (addr & 0x1FFFU);
    } else {
        offset = static_cast<uint32_t>(mmc3_chr_bank(addr)) * 1024U + (addr & 0x03FFU);
    }

    if ((state_.outer[0] & 0x80U) != 0U)
        replace_pin(offset, 17U, (state_.outer[0] & 0x08U) != 0U);
    replace_pin(offset, 18U, (state_.outer[0] & 0x10U) != 0U);
    replace_pin(offset, 19U, (state_.outer[0] & 0x20U) != 0U);
    if (submapper_in(4U, 5U)) {
        replace_pin(offset, 20U, (state_.outer[0] & 0x02U) != 0U);
        replace_pin(offset, 21U, (state_.outer[0] & 0x04U) != 0U);
    }
    return offset;
}

bool Coolboy::mixed_chr_ram_selected(Address addr) const {
    return !chr_rom_.empty() && !chr_ram_.empty() && (state_.outer[4] & 1U) != 0U &&
           (mmc3_chr_bank(addr) >> 1U) == (state_.outer[4] >> 1U);
}

Byte Coolboy::read_chr(Address addr) {
    if (mixed_chr_ram_selected(addr)) {
        const std::size_t offset = (mmc3_chr_bank(addr) & 1U) * 1024U + (addr & 0x03FFU);
        return chr_ram_[offset % chr_ram_.size()];
    }
    if (!chr_rom_.empty())
        return chr_rom_[mapped_chr_offset(addr) % chr_rom_.size()];
    return chr_ram_[mapped_chr_offset(addr) % chr_ram_.size()];
}

void Coolboy::write_chr(Address addr, Byte value) {
    if (submapper_in(8U, 9U) && (state_.outer[0] & 0x20U) != 0U)
        return;
    if (mixed_chr_ram_selected(addr)) {
        const std::size_t offset = (mmc3_chr_bank(addr) & 1U) * 1024U + (addr & 0x03FFU);
        chr_ram_[offset % chr_ram_.size()] = value;
    } else if (chr_rom_.empty()) {
        chr_ram_[mapped_chr_offset(addr) % chr_ram_.size()] = value;
    }
}

std::size_t Coolboy::mapped_prg_offset(Address addr) const {
    const unsigned slot = (addr - 0x8000U) >> 13U;
    uint32_t offset = 0U;
    if (gnrom_mode()) {
        offset = ((static_cast<uint32_t>(state_.outer[3]) >> 2U) & 3U) << 14U;
        const bool nrom256 = submapper_in(2U, 3U) ? (state_.outer[1] & 0x10U) == 0U
                                                  : (state_.outer[1] & 0x02U) != 0U;
        offset |= nrom256 ? addr & 0x7FFFU
                          : (static_cast<uint32_t>(state_.outer[3] & 2U) << 13U) | (addr & 0x3FFFU);
    } else {
        unsigned bank = 0U;
        if (slot == 1U)
            bank = state_.banks[7];
        else if (slot == 3U)
            bank = weird_mode() && (state_.bank_select & 0x40U) == 0U ? 0U : 0xFFU;
        else if ((slot == 0U) == ((state_.bank_select & 0x40U) != 0U))
            bank = weird_mode() && (state_.bank_select & 0x40U) == 0U ? 0U : 0xFEU;
        else
            bank = state_.banks[6];
        offset = bank * 8192U + (addr & 0x1FFFU);
    }

    if ((state_.outer[0] & 0x40U) != 0U)
        replace_pin(offset, 17U, (state_.outer[0] & 0x01U) != 0U);
    if ((state_.outer[1] & 0x80U) != 0U)
        replace_pin(offset, 18U, (state_.outer[0] & 0x02U) != 0U);
    if ((state_.outer[1] & 0x40U) == 0U)
        replace_pin(offset, 19U, (state_.outer[0] & 0x04U) != 0U);
    if ((state_.outer[1] & 0x20U) == 0U) {
        const bool a20 = submapper_in(4U, 5U)   ? (state_.outer[0] & 0x10U) != 0U
                         : submapper_in(8U, 9U) ? false
                         : submapper_in(2U, 3U) ? (state_.outer[1] & 0x02U) != 0U
                                                : (state_.outer[1] & 0x10U) != 0U;
        replace_pin(offset, 20U, a20);
    }

    if (submapper_in(4U, 5U)) {
        replace_pin(offset, 21U, (state_.outer[0] & 0x20U) != 0U);
        replace_pin(offset, 22U, false);
        replace_pin(offset, 23U, false);
        replace_pin(offset, 24U, false);
    } else {
        if (!submapper_in(8U, 9U)) {
            replace_pin(offset, 21U, (state_.outer[1] & 0x04U) != 0U);
            replace_pin(offset, 22U, (state_.outer[1] & 0x08U) != 0U);
            replace_pin(offset, 23U, (state_.outer[0] & 0x10U) != 0U);
            replace_pin(offset, 24U, (state_.outer[0] & 0x20U) != 0U);
        } else {
            replace_pin(offset, 21U, false);
            replace_pin(offset, 22U, false);
            replace_pin(offset, 23U, false);
            replace_pin(offset, 24U, false);
        }
    }

    if ((state_.outer[5] & 0x10U) != 0U) {
        const auto chr = static_cast<uint32_t>(mapped_chr_offset(state_.observed_ppu_address));
        replace_pin(offset, 18U, pin(chr, 16U));
        replace_pin(offset, 19U, pin(chr, 17U));
    }

    if (submapper_in(0U, 1U)) {
        const bool a25 = (state_.outer[1] & 1U) != 0U   ? false
                         : (state_.outer[3] & 1U) != 0U ? true
                                                        : (addr & 1U) != 0U;
        replace_pin(offset, 25U, a25);
    }

    if (submapper_in(6U, 7U)) {
        const bool second_chip = (state_.outer[0] & 0x80U) != 0U
                                     ? (state_.outer[0] & 0x08U) != 0U
                                     : (mmc3_chr_bank(state_.observed_ppu_address) & 0x80U) != 0U;
        const std::size_t half = prg_rom_.size() / 2U;
        return (second_chip ? half : 0U) + offset % half;
    }
    return offset % prg_rom_.size();
}

MirrorMode Coolboy::mirror_mode() const {
    if (submapper_in(10U, 11U) && (state_.outer[0] & 0x40U) == 0U)
        return (state_.outer[0] & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    return state_.mirror;
}

bool Coolboy::maps_expansion(Address addr) const {
    return addr >= 0x5000U && addr < 0x6000U &&
           ((submapper_ & 1U) != 0U || extra_wram_mapped(addr));
}

Byte Coolboy::expansion_read_mask(Address addr) const {
    return extra_wram_mapped(addr) ? 0xFFU : 0U;
}

Byte Coolboy::read_expansion(Address addr) {
    return extra_wram_mapped(addr) ? prg_ram_[addr & 0x0FFFU] : 0U;
}

void Coolboy::write_expansion(Address addr, Byte value) {
    if ((submapper_ & 1U) != 0U && is_outer_register(addr))
        write_outer(addr & 7U, value);
    write_ram(addr, value);
}

void Coolboy::observe_ppu_address(Address addr) {
    state_.observed_ppu_address = addr & 0x3FFFU;
}

void Coolboy::power_on() {
    reset();
    if (!persistent_wram_)
        std::ranges::fill(prg_ram_, 0U);
    std::ranges::fill(chr_ram_, 0U);
}

void Coolboy::reset() {
    state_ = RuntimeState{};
    state_.mirror = initial_mirror_;
}

void Coolboy::clock_irq_counter() {
    const auto previous = state_.irq_counter;
    if (state_.irq_counter == 0U || state_.irq_reload_flag) {
        state_.irq_counter = state_.irq_reload;
        state_.irq_reload_flag = false;
    } else {
        --state_.irq_counter;
    }
    if (previous != 0U && state_.irq_counter == 0U && state_.irq_reload != 0U && state_.irq_enabled)
        state_.irq_pending = true;
}

std::span<const Byte> Coolboy::battery_ram() const {
    return persistent_wram_ ? std::span<const Byte>{prg_ram_} : std::span<const Byte>{};
}

void Coolboy::set_battery_ram(std::span<const Byte> data) {
    if (persistent_wram_)
        std::ranges::copy(data.first(std::min(data.size(), prg_ram_.size())), prg_ram_.begin());
}

uint32_t Coolboy::geometry_tag() const {
    return kStateTag | submapper_ | (static_cast<uint32_t>(initial_mirror_) << 8U) |
           (persistent_wram_ ? 0x10000U : 0U);
}

void Coolboy::save_state(StateWriter& writer) const {
    writer.write(geometry_tag());
    writer.write<uint32_t>(prg_rom_.size());
    writer.write<uint32_t>(chr_rom_.size());
    writer.write<uint32_t>(prg_ram_.size());
    writer.write<uint32_t>(chr_ram_.size());
    writer.write_array(state_.outer);
    writer.write(state_.bank_select);
    writer.write_array(state_.banks);
    writer.write<Byte>(static_cast<Byte>(state_.mirror));
    writer.write(state_.irq_counter);
    writer.write(state_.irq_reload);
    writer.write<Byte>((state_.irq_reload_flag ? 1U : 0U) | (state_.irq_enabled ? 2U : 0U) |
                       (state_.irq_pending ? 4U : 0U));
    writer.write<Byte>((state_.wram_enabled ? 0x80U : 0U) |
                       (state_.wram_write_protected ? 0x40U : 0U));
    writer.write(state_.observed_ppu_address);
    writer.write_bytes(prg_ram_);
    writer.write_bytes(chr_ram_);
}

void Coolboy::load_state(StateReader& reader) {
    const auto tag = reader.read<uint32_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    const auto wram_size = reader.read<uint32_t>();
    const auto chr_ram_size = reader.read<uint32_t>();
    RuntimeState next;
    reader.read_array(next.outer);
    reader.read(next.bank_select);
    reader.read_array(next.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    const auto irq = reader.read<Byte>();
    const auto wram = reader.read<Byte>();
    reader.read(next.observed_ppu_address);
    std::vector<Byte> prg_ram, chr_ram;
    reader.read_bytes(prg_ram, kWramSize);
    reader.read_bytes(chr_ram, kMaxChrRamSize);

    if (!reader.ok() || tag != geometry_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || wram_size != prg_ram_.size() ||
        chr_ram_size != chr_ram_.size() || prg_ram.size() != prg_ram_.size() ||
        chr_ram.size() != chr_ram_.size() || (next.bank_select & 0x38U) != 0U ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U) || (wram & 0x3FU) != 0U ||
        next.observed_ppu_address > 0x3FFFU) {
        reader.fail();
        return;
    }

    next.mirror = static_cast<MirrorMode>(mirror);
    next.irq_reload_flag = (irq & 1U) != 0U;
    next.irq_enabled = (irq & 2U) != 0U;
    next.irq_pending = (irq & 4U) != 0U;
    next.wram_enabled = (wram & 0x80U) != 0U;
    next.wram_write_protected = (wram & 0x40U) != 0U;
    state_ = next;
    prg_ram_ = std::move(prg_ram);
    chr_ram_ = std::move(chr_ram);
}

} // namespace mapperbus::core
