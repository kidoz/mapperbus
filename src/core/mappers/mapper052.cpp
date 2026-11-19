#include "core/mappers/mapper052.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr bool power_of_two(std::size_t size) {
    return size != 0U && std::has_single_bit(size);
}

} // namespace

Realtec8213::Realtec8213(const INesHeader& header,
                         std::vector<Byte> prg_rom,
                         std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_size_(header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size : 0x2000U),
      nvram_size_(header.is_nes2       ? header.prg_nvram_size
                  : header.has_battery ? 0x2000U
                                       : 0U),
      submapper_(header.is_nes2 ? header.submapper : 0U),
      chr_ram_present_(submapper_ == 13U || (submapper_ == 14U && header.chr_ram_size == 0x2000U)),
      initial_mirror_(header.mirror_mode == MirrorMode::Vertical ? MirrorMode::Vertical
                                                                 : MirrorMode::Horizontal),
      mirror_mode_(initial_mirror_) {
    reset();
}

Result<void> Realtec8213::validate_geometry(const INesHeader& header,
                                            std::size_t prg_size,
                                            std::size_t chr_size) {
    const Byte submapper = header.is_nes2 ? header.submapper : 0U;
    if (submapper != 0U && submapper != 13U && submapper != 14U)
        return std::unexpected("Mapper 52 supports submappers 0, 13, and 14");
    if (!power_of_two(prg_size) || !power_of_two(chr_size) || prg_size < 0x8000U ||
        prg_size > 0x100000U || chr_size < 0x2000U || chr_size > 0x100000U)
        return std::unexpected(
            "Mapper 52 requires power-of-two PRG/CHR ROMs within the connected address range");
    if (submapper == 13U && (prg_size != 0x80000U || chr_size != 0x80000U))
        return std::unexpected("Mapper 52 submapper 13 requires 512 KiB PRG and CHR ROMs");
    if (submapper == 14U && (prg_size != 0x100000U || chr_size != 0x100000U))
        return std::unexpected("Mapper 52 submapper 14 requires 1 MiB PRG and CHR ROMs");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 52 requires horizontal or vertical CIRAM mirroring");

    if (header.is_nes2) {
        if ((header.prg_ram_size != 0U && header.prg_ram_size != 0x2000U) ||
            (header.prg_nvram_size != 0U && header.prg_nvram_size != 0x2000U) ||
            (header.prg_ram_size != 0U && header.prg_nvram_size != 0U))
            return std::unexpected("Mapper 52 supports no PRG RAM or one 8 KiB PRG RAM chip");
        if (header.has_battery != (header.prg_nvram_size != 0U))
            return std::unexpected("Mapper 52 battery metadata must match PRG NVRAM population");
        const auto expected_chr_ram = submapper == 13U ? 0x2000U : 0U;
        if (header.chr_ram_size != expected_chr_ram &&
            !(submapper == 14U && header.chr_ram_size == 0x2000U))
            return std::unexpected("Mapper 52 CHR RAM population does not match its submapper");
        if (header.chr_nvram_size != 0U)
            return std::unexpected("Mapper 52 does not support battery-backed CHR RAM");
    }
    return {};
}

unsigned Realtec8213::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / 0x2000U;
    if (slot == 1U)
        return bank_registers_[7U] & 0x1FU;
    if ((!prg_mode_ && slot == 0U) || (prg_mode_ && slot == 2U))
        return bank_registers_[6U] & 0x1FU;
    return slot == 3U ? 31U : 30U;
}

unsigned Realtec8213::outer_prg_bank(unsigned inner_bank) const {
    const bool force_a17 = (outer_bank_ & 0x08U) != 0U;
    const unsigned a17 = force_a17 ? outer_bank_ & 1U : (inner_bank >> 4U) & 1U;
    const unsigned a18 = (outer_bank_ >> 1U) & 1U;
    const unsigned a19 = (outer_bank_ >> 2U) & 1U;
    return (inner_bank & 0x0FU) | (a17 << 4U) | (a18 << 5U) | (a19 << 6U);
}

Byte Realtec8213::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!prg_ram_enabled_ || prg_ram_size_ == 0U)
            return 0U;
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U || prg_rom_.empty())
        return 0U;
    const auto bank = outer_prg_bank(inner_prg_bank(addr));
    const auto offset = bank * 0x2000U + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Realtec8213::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_ && (outer_bank_ & 0x80U) == 0U)
            outer_bank_ = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            bank_select_ = value & 7U;
            prg_mode_ = (value & 0x40U) != 0U;
            chr_inversion_ = (value & 0x80U) != 0U;
        } else {
            bank_registers_[bank_select_] =
                bank_select_ >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        }
    } else if (addr < 0xC000U) {
        if (even)
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        else {
            prg_ram_enabled_ = (value & 0x80U) != 0U;
            prg_ram_write_protected_ = (value & 0x40U) != 0U;
        }
    } else if (addr < 0xE000U) {
        if (even)
            irq_reload_ = value;
        else {
            irq_reload_pending_ = true;
            irq_counter_ = 0U;
        }
    } else if (even) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

bool Realtec8213::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && prg_ram_enabled_ && prg_ram_size_ != 0U);
}

unsigned Realtec8213::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    if (slot < 4U)
        return (bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U);
    return bank_registers_[slot - 2U];
}

unsigned Realtec8213::outer_chr_bank(unsigned inner_bank) const {
    const bool force_a17 = (outer_bank_ & 0x40U) != 0U;
    const unsigned a17 = force_a17 ? (outer_bank_ >> 4U) & 1U : (inner_bank >> 4U) & 1U;
    const unsigned a18 = submapper_ == 14U ? (outer_bank_ >> 1U) & 1U : (outer_bank_ >> 5U) & 1U;
    const unsigned a19 = (outer_bank_ >> 2U) & 1U;
    return (inner_bank & 0x0FU) | (a17 << 4U) | (a18 << 5U) | (a19 << 6U);
}

bool Realtec8213::chr_ram_selected() const {
    if (!chr_ram_present_)
        return false;
    if (submapper_ == 14U)
        return (outer_bank_ & 0x20U) != 0U;
    return submapper_ == 13U && (outer_bank_ & 3U) == 3U;
}

Byte Realtec8213::read_chr(Address addr) {
    if (chr_ram_selected())
        return chr_ram_[addr & 0x1FFFU];
    if (chr_rom_.empty())
        return 0U;
    const auto bank = outer_chr_bank(inner_chr_bank(addr));
    const auto offset = bank * 0x400U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Realtec8213::write_chr(Address addr, Byte value) {
    if (chr_ram_selected())
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Realtec8213::mirror_mode() const {
    return mirror_mode_;
}

void Realtec8213::power_on() {
    reset();
    if (nvram_size_ == 0U)
        prg_ram_.fill(0U);
    if (chr_ram_present_)
        chr_ram_.fill(0U);
}

void Realtec8213::reset() {
    outer_bank_ = 0U;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_pending_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    mirror_mode_ = initial_mirror_;
}

bool Realtec8213::irq_pending() const {
    return irq_pending_;
}

void Realtec8213::acknowledge_irq() {
    irq_pending_ = false;
}

void Realtec8213::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_pending_) {
        irq_counter_ = irq_reload_;
        irq_reload_pending_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

std::uint16_t Realtec8213::memory_tag() const {
    return static_cast<std::uint16_t>((initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (prg_ram_size_ != 0U ? 2U : 0U) |
                                      (nvram_size_ != 0U ? 4U : 0U) | (chr_ram_present_ ? 8U : 0U));
}

void Realtec8213::save_state(StateWriter& writer) const {
    writer.write<std::uint16_t>(0x3400U | submapper_);
    writer.write<std::uint32_t>(prg_rom_.size());
    writer.write<std::uint32_t>(chr_rom_.size());
    writer.write(memory_tag());
    writer.write(outer_bank_);
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_pending_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
    if (prg_ram_size_ != 0U)
        writer.write_array(prg_ram_);
    if (chr_ram_present_)
        writer.write_array(chr_ram_);
}

void Realtec8213::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory = reader.read<std::uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    std::array<Byte, 0x2000> prg_ram{};
    std::array<Byte, 0x2000> chr_ram{};
    if (prg_ram_size_ != 0U)
        reader.read_array(prg_ram);
    if (chr_ram_present_)
        reader.read_array(chr_ram);

    if (!reader.ok() || tag != (0x3400U | submapper_) || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory != memory_tag() || select > 7U ||
        (mode & 0x3FU) != 0U || banks[6U] > 0x3FU || banks[7U] > 0x3FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer;
    bank_select_ = select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_pending_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    prg_ram_enabled_ = (ram & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram & 0x40U) != 0U;
    if (prg_ram_size_ != 0U)
        prg_ram_ = prg_ram;
    if (chr_ram_present_)
        chr_ram_ = chr_ram;
}

std::span<const Byte> Realtec8213::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Realtec8213::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), nvram_size_), prg_ram_.begin());
}

} // namespace mapperbus::core
