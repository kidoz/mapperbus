#include "core/mappers/mapper269.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr uint16_t kStateTag = 0x2690U;

Byte unscramble_chr(Byte value) {
    const unsigned data = value;
    return static_cast<Byte>(((data & 1U) << 6U) | ((data & 2U) << 3U) | (data & 4U) |
                             ((data & 8U) >> 3U) | ((data & 16U) >> 3U) | ((data & 32U) >> 2U) |
                             ((data & 64U) >> 1U) | (data & 128U));
}

} // namespace

Mapper269::Mapper269(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), battery_backed_(header.has_battery) {
    reset();
    registers_.mirror = initial_mirror_;
}

Result<void> Mapper269::validate_geometry(const INesHeader& h,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!h.is_nes2 || h.mapper_number != 269U || h.submapper != 0U)
        return std::unexpected("Mapper 269 requires NES 2.0 submapper 0");
    if (!std::has_single_bit(prg_size) || prg_size < 32768U || prg_size > 524288U ||
        !std::has_single_bit(chr_size) || chr_size < 8192U || chr_size > 262144U)
        return std::unexpected(
            "Mapper 269 requires power-of-two 32-512 KiB PRG and 8-256 KiB CHR ROM");
    if (h.prg_ram_size != (h.has_battery ? 0U : 8192U) ||
        h.prg_nvram_size != (h.has_battery ? 8192U : 0U) || h.chr_ram_size != 0U ||
        h.chr_nvram_size != 0U)
        return std::unexpected("Mapper 269 requires 8 KiB WRAM and no CHR RAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 269 supports H/V startup mirroring");
    return {};
}

Byte Mapper269::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return registers_.ram_enabled ? wram_[addr & 0x1FFFU] : 0U;
    if (addr < 0x8000U || prg_rom_.empty())
        return 0U;
    const unsigned slot = (addr - 0x8000U) / 0x2000U;
    unsigned inner = 0U;
    if (slot == 1U)
        inner = registers_.banks[7U];
    else if (slot == 3U)
        inner = 63U;
    else if ((slot == 0U) == registers_.prg_mode)
        inner = 62U;
    else
        inner = registers_.banks[6U];
    const unsigned mask = registers_.outer[3U] & 0x3FU;
    const unsigned outer = static_cast<unsigned>(registers_.outer[1U]) |
                           ((static_cast<unsigned>(registers_.outer[3U]) & 0xC0U) << 2U);
    const unsigned bank = (inner & (63U ^ mask)) | (outer & mask);
    const auto offset =
        (static_cast<std::size_t>(bank) * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size();
    return prg_rom_[offset];
}

void Mapper269::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (registers_.ram_enabled && !registers_.ram_write_protected)
            wram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr < 0x8000U)
        return;
    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.bank_select = value & 7U;
        registers_.prg_mode = (value & 0x40U) != 0U;
        registers_.chr_inversion = (value & 0x80U) != 0U;
        break;
    case 0x8001U:
        registers_.banks[registers_.bank_select] =
            registers_.bank_select >= 6U ? value & 0x3FU : value;
        break;
    case 0xA000U:
        registers_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        registers_.ram_enabled = (value & 0x80U) != 0U;
        registers_.ram_write_protected = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        registers_.irq_latch = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_reload = true;
        break;
    case 0xE000U:
        registers_.irq_enabled = false;
        registers_.irq_pending = false;
        break;
    case 0xE001U:
        registers_.irq_enabled = true;
        break;
    default:
        break;
    }
}

bool Mapper269::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U && registers_.ram_enabled);
}

unsigned Mapper269::inner_chr_bank(Address addr) const {
    const auto slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ (registers_.chr_inversion ? 4U : 0U);
    if (slot < 4U)
        return (registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U);
    return registers_.banks[slot - 2U];
}

unsigned Mapper269::chr_outer_mask() const {
    const unsigned bits = registers_.outer[2U] & 0x0FU;
    if (bits == 0U)
        return 0U;
    if (bits >= 8U)
        return 0xFFU;
    return (0xFFU << (8U - bits)) & 0xFFU;
}

Byte Mapper269::read_chr(Address addr) {
    if (chr_rom_.empty())
        return 0U;
    const unsigned mask = chr_outer_mask();
    const unsigned outer = static_cast<unsigned>(registers_.outer[0U]) |
                           ((static_cast<unsigned>(registers_.outer[2U]) & 0xF0U) << 4U) |
                           ((static_cast<unsigned>(registers_.outer[3U]) & 0xC0U) << 6U);
    const unsigned bank = (inner_chr_bank(addr) & (0xFFU ^ mask)) | (outer & mask);
    const auto offset =
        (static_cast<std::size_t>(bank) * 0x400U + (addr & 0x3FFU)) % chr_rom_.size();
    return unscramble_chr(chr_rom_[offset]);
}

void Mapper269::write_chr(Address, Byte) {}

MirrorMode Mapper269::mirror_mode() const {
    return registers_.mirror;
}

bool Mapper269::maps_expansion(Address addr) const {
    return (addr & 0xF008U) == 0x5000U;
}

Byte Mapper269::expansion_read_mask(Address) const {
    return 0U;
}

Byte Mapper269::read_expansion(Address) {
    return 0U;
}

void Mapper269::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr))
        return;
    registers_.outer[registers_.outer_index] = value;
    registers_.outer_index = (registers_.outer_index + 1U) & 3U;
}

void Mapper269::power_on() {
    reset();
    registers_.mirror = initial_mirror_;
    if (!battery_backed_)
        wram_.fill(0U);
}

void Mapper269::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

void Mapper269::clock_irq_counter() {
    if (registers_.irq_counter == 0U || registers_.irq_reload) {
        registers_.irq_counter = registers_.irq_latch;
        registers_.irq_reload = false;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && registers_.irq_enabled)
        registers_.irq_pending = true;
}

std::span<const Byte> Mapper269::battery_ram() const {
    return battery_backed_ ? std::span<const Byte>(wram_) : std::span<const Byte>();
}

void Mapper269::set_battery_ram(std::span<const Byte> data) {
    if (battery_backed_ && data.size() == wram_.size())
        std::copy(data.begin(), data.end(), wram_.begin());
}

uint16_t Mapper269::state_tag() const {
    return static_cast<uint16_t>(kStateTag | (battery_backed_ ? 1U : 0U) |
                                 (initial_mirror_ == MirrorMode::Vertical ? 2U : 0U));
}

void Mapper269::save_state(StateWriter& w) const {
    w.write(state_tag());
    w.write<uint32_t>(static_cast<uint32_t>(prg_rom_.size()));
    w.write<uint32_t>(static_cast<uint32_t>(chr_rom_.size()));
    w.write_array(wram_);
    w.write_array(registers_.outer);
    w.write(registers_.outer_index);
    w.write(registers_.bank_select);
    w.write<Byte>((registers_.prg_mode ? 0x40U : 0U) | (registers_.chr_inversion ? 0x80U : 0U));
    w.write_array(registers_.banks);
    w.write<Byte>(static_cast<Byte>(registers_.mirror));
    w.write(registers_.irq_counter);
    w.write(registers_.irq_latch);
    w.write<Byte>((registers_.irq_reload ? 1U : 0U) | (registers_.irq_enabled ? 2U : 0U) |
                  (registers_.irq_pending ? 4U : 0U));
    w.write<Byte>((registers_.ram_enabled ? 0x80U : 0U) |
                  (registers_.ram_write_protected ? 0x40U : 0U));
}

void Mapper269::load_state(StateReader& r) {
    const auto tag = r.read<uint16_t>();
    const auto prg_size = r.read<uint32_t>();
    const auto chr_size = r.read<uint32_t>();
    std::array<Byte, 0x2000> wram{};
    r.read_array(wram);
    Registers next;
    r.read_array(next.outer);
    r.read(next.outer_index);
    r.read(next.bank_select);
    const auto mode = r.read<Byte>();
    r.read_array(next.banks);
    const auto mirror = r.read<Byte>();
    r.read(next.irq_counter);
    r.read(next.irq_latch);
    const auto irq = r.read<Byte>();
    const auto ram = r.read<Byte>();
    if (!r.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || next.outer_index > 3U || next.bank_select > 7U ||
        (mode & 0x3FU) != 0U || next.banks[6U] > 0x3FU || next.banks[7U] > 0x3FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U) {
        r.fail();
        return;
    }
    next.prg_mode = (mode & 0x40U) != 0U;
    next.chr_inversion = (mode & 0x80U) != 0U;
    next.mirror = static_cast<MirrorMode>(mirror);
    next.irq_reload = (irq & 1U) != 0U;
    next.irq_enabled = (irq & 2U) != 0U;
    next.irq_pending = (irq & 4U) != 0U;
    next.ram_enabled = (ram & 0x80U) != 0U;
    next.ram_write_protected = (ram & 0x40U) != 0U;
    wram_ = wram;
    registers_ = next;
}

} // namespace mapperbus::core
