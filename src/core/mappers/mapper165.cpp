#include "core/mappers/mapper165.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::uint16_t kStateTag = 0xA500U;
constexpr std::size_t kWramSize = 0x2000U;

struct Mapper165State {
    std::uint16_t tag = 0U;
    std::uint32_t prg_size = 0U;
    std::uint32_t chr_size = 0U;
    std::uint32_t ram_size = 0U;
    std::uint32_t nvram_size = 0U;
    Byte initial_mirror = 0U;
    Byte select = 0U;
    std::array<Byte, 8> banks{};
    Byte mirror = 0U;
    Byte ram_flags = 0U;
    Byte irq_counter = 0U;
    Byte irq_reload = 0U;
    Byte irq_flags = 0U;
    std::array<Byte, 2> latches{};
    std::vector<Byte> prg_ram;
    std::array<Byte, 0x1000> chr_ram{};
};

} // namespace

Mapper165::Mapper165(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    if (header.is_nes2) {
        prg_ram_.resize(header.prg_ram_size + header.prg_nvram_size);
        nvram_size_ = header.prg_nvram_size;
    } else {
        prg_ram_.resize(kWramSize);
        nvram_size_ = header.has_battery ? kWramSize : 0U;
    }
    power_on();
}

Result<void> Mapper165::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 165U)
        return std::unexpected("Mapper165 supports mapper 165 only");
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 165 supports submapper 0");
    if (prg_size < 0x4000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U)
        return std::unexpected("Mapper 165 requires 16-512 KiB PRG ROM in 8 KiB units");
    if (chr_size < 0x1000U || chr_size > 0x100000U || chr_size % 0x1000U != 0U)
        return std::unexpected("Mapper 165 requires 4 KiB-1 MiB CHR ROM in 4 KiB units");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 165 requires H/V startup mirroring");
    if (header.chr_nvram_size != 0U || (header.is_nes2 && header.chr_ram_size != 0x1000U))
        return std::unexpected("Mapper 165 requires exactly 4 KiB volatile CHR RAM");
    if (!header.is_nes2)
        return {};

    const auto ram_size = header.prg_ram_size + header.prg_nvram_size;
    if (ram_size > kWramSize || (ram_size != 0U && ram_size != kWramSize) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U))
        return std::unexpected("Mapper 165 supports zero or one 8 KiB PRG RAM chip");
    if (header.has_battery != (header.prg_nvram_size != 0U))
        return std::unexpected("Mapper 165 battery flag and PRG NVRAM declaration disagree");
    return {};
}

std::size_t Mapper165::prg_ram_size() const {
    return prg_ram_.size();
}

bool Mapper165::maps_prg(Address addr) const {
    if (addr >= 0x8000U)
        return true;
    return addr >= 0x6000U && addr < 0x8000U && prg_ram_enabled_ && prg_ram_size() != 0U;
}

Byte Mapper165::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!maps_prg(addr))
            return 0U;
        return prg_ram_[addr - 0x6000U];
    }
    if (addr < 0x8000U)
        return 0U;

    const auto bank_count = prg_rom_.size() / 0x2000U;
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const auto r6 = static_cast<std::size_t>(bank_registers_[6] % bank_count);
    const auto r7 = static_cast<std::size_t>(bank_registers_[7] % bank_count);
    const std::array<std::size_t, 4> banks =
        prg_mode_ ? std::array<std::size_t, 4>{bank_count - 2U, r7, r6, bank_count - 1U}
                  : std::array<std::size_t, 4>{r6, r7, bank_count - 2U, bank_count - 1U};
    const auto offset = banks[slot] * 0x2000U + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper165::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_ && prg_ram_size() != 0U)
            prg_ram_[addr - 0x6000U] = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    const auto port = static_cast<unsigned>((addr >> 12U) & 6U) | (addr & 1U);
    switch (port) {
    case 0U:
        bank_select_ = value & 7U;
        prg_mode_ = (value & 0x40U) != 0U;
        chr_inversion_ = (value & 0x80U) != 0U;
        break;
    case 1U:
        bank_registers_[bank_select_] = bank_select_ >= 6U ? value & 0x3FU : value;
        break;
    case 2U:
        mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 3U:
        prg_ram_enabled_ = (value & 0x80U) != 0U;
        prg_ram_write_protected_ = (value & 0x40U) != 0U;
        break;
    case 4U:
        irq_reload_ = value;
        break;
    case 5U:
        irq_counter_ = 0U;
        irq_reload_pending_ = true;
        break;
    case 6U:
        irq_enabled_ = false;
        irq_pending_ = false;
        break;
    case 7U:
        irq_enabled_ = true;
        break;
    default:
        std::unreachable();
    }
}

Byte Mapper165::chr_bank(Address addr) const {
    auto effective = addr & 0x1FFFU;
    if (chr_inversion_)
        effective ^= 0x1000U;
    const auto half = static_cast<unsigned>(effective >> 12U);
    const auto reg = half == 0U ? chr_latches_[0] : (chr_latches_[1] != 0U ? 4U : 2U);
    return bank_registers_[reg];
}

Byte Mapper165::read_chr(Address addr) {
    const auto effective = static_cast<Address>((addr & 0x1FFFU) ^ (chr_inversion_ ? 0x1000U : 0U));
    const auto bank = chr_bank(addr);
    if (bank == 0U)
        return chr_ram_[effective & 0x0FFFU];
    const auto offset = static_cast<std::size_t>(bank) * 0x1000U + (effective & 0x0FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper165::write_chr(Address addr, Byte value) {
    if (chr_bank(addr) != 0U)
        return;
    const auto effective = static_cast<Address>((addr & 0x1FFFU) ^ (chr_inversion_ ? 0x1000U : 0U));
    chr_ram_[effective & 0x0FFFU] = value;
}

MirrorMode Mapper165::mirror_mode() const {
    return mirror_mode_;
}

bool Mapper165::observes_ppu_reads() const {
    return true;
}

void Mapper165::observe_ppu_read(Address addr) {
    if (addr >= 0x2000U)
        return;
    const auto half = static_cast<unsigned>(addr >> 12U);
    if (half == 0U) {
        if (addr == 0x0FD8U)
            chr_latches_[0] = 0U;
        else if (addr == 0x0FE8U)
            chr_latches_[0] = 1U;
    } else if ((addr & 0x0FF8U) == 0x0FD8U) {
        chr_latches_[1] = 0U;
    } else if ((addr & 0x0FF8U) == 0x0FE8U) {
        chr_latches_[1] = 1U;
    }
}

void Mapper165::power_on() {
    reset();
    if (nvram_size_ == 0U)
        std::ranges::fill(prg_ram_, 0U);
    chr_ram_.fill(0U);
}

void Mapper165::reset() {
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    chr_latches_.fill(0U);
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_pending_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
    mirror_mode_ = initial_mirror_;
}

bool Mapper165::irq_pending() const {
    return irq_pending_;
}

void Mapper165::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper165::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_pending_) {
        irq_counter_ = irq_reload_;
        irq_reload_pending_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

std::span<const Byte> Mapper165::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper165::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == nvram_size_)
        std::ranges::copy(data, prg_ram_.begin());
}

void Mapper165::save_state(StateWriter& writer) const {
    Mapper165State state;
    state.tag = kStateTag;
    state.prg_size = static_cast<std::uint32_t>(prg_rom_.size());
    state.chr_size = static_cast<std::uint32_t>(chr_rom_.size());
    state.ram_size = static_cast<std::uint32_t>(prg_ram_size());
    state.nvram_size = static_cast<std::uint32_t>(nvram_size_);
    state.initial_mirror = static_cast<Byte>(initial_mirror_);
    state.select =
        static_cast<Byte>(bank_select_ | (prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    state.banks = bank_registers_;
    state.mirror = static_cast<Byte>(mirror_mode_);
    state.ram_flags = static_cast<Byte>((prg_ram_enabled_ ? 0x80U : 0U) |
                                        (prg_ram_write_protected_ ? 0x40U : 0U));
    state.irq_counter = irq_counter_;
    state.irq_reload = irq_reload_;
    state.irq_flags = static_cast<Byte>((irq_reload_pending_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                                        (irq_pending_ ? 4U : 0U));
    state.latches = chr_latches_;
    state.prg_ram = prg_ram_;
    state.chr_ram = chr_ram_;

    writer.write(state.tag);
    writer.write(state.prg_size);
    writer.write(state.chr_size);
    writer.write(state.ram_size);
    writer.write(state.nvram_size);
    writer.write(state.initial_mirror);
    writer.write(state.select);
    writer.write_array(state.banks);
    writer.write(state.mirror);
    writer.write(state.ram_flags);
    writer.write(state.irq_counter);
    writer.write(state.irq_reload);
    writer.write(state.irq_flags);
    writer.write_array(state.latches);
    writer.write_bytes(state.prg_ram);
    writer.write_array(state.chr_ram);
}

void Mapper165::load_state(StateReader& reader) {
    Mapper165State state;
    reader.read(state.tag);
    reader.read(state.prg_size);
    reader.read(state.chr_size);
    reader.read(state.ram_size);
    reader.read(state.nvram_size);
    reader.read(state.initial_mirror);
    reader.read(state.select);
    reader.read_array(state.banks);
    reader.read(state.mirror);
    reader.read(state.ram_flags);
    reader.read(state.irq_counter);
    reader.read(state.irq_reload);
    reader.read(state.irq_flags);
    reader.read_array(state.latches);
    reader.read_bytes(state.prg_ram, kWramSize);
    reader.read_array(state.chr_ram);

    const auto initial_mirror = static_cast<MirrorMode>(state.initial_mirror);
    const auto mirror = static_cast<MirrorMode>(state.mirror);
    const bool valid_mirror = initial_mirror == initial_mirror_ &&
                              (mirror == MirrorMode::Horizontal || mirror == MirrorMode::Vertical);
    const bool valid_latches = state.latches[0] <= 1U && state.latches[1] <= 1U;
    const bool valid_irq = (state.irq_flags & ~7U) == 0U &&
                           ((state.irq_flags & 4U) == 0U || (state.irq_flags & 2U) != 0U);
    if (!reader.ok() || state.tag != kStateTag || state.prg_size != prg_rom_.size() ||
        state.chr_size != chr_rom_.size() || state.ram_size != prg_ram_size() ||
        state.nvram_size != nvram_size_ || state.prg_ram.size() != prg_ram_size() ||
        !valid_mirror || (state.select & 0x38U) != 0U || state.banks[6] > 0x3FU ||
        state.banks[7] > 0x3FU || (state.ram_flags & 0x3FU) != 0U || !valid_irq || !valid_latches) {
        reader.fail();
        return;
    }

    initial_mirror_ = initial_mirror;
    bank_select_ = state.select & 7U;
    prg_mode_ = (state.select & 0x40U) != 0U;
    chr_inversion_ = (state.select & 0x80U) != 0U;
    bank_registers_ = state.banks;
    mirror_mode_ = mirror;
    prg_ram_enabled_ = (state.ram_flags & 0x80U) != 0U;
    prg_ram_write_protected_ = (state.ram_flags & 0x40U) != 0U;
    irq_counter_ = state.irq_counter;
    irq_reload_ = state.irq_reload;
    irq_reload_pending_ = (state.irq_flags & 1U) != 0U;
    irq_enabled_ = (state.irq_flags & 2U) != 0U;
    irq_pending_ = (state.irq_flags & 4U) != 0U;
    chr_latches_ = state.latches;
    prg_ram_ = std::move(state.prg_ram);
    chr_ram_ = state.chr_ram;
}

} // namespace mapperbus::core
