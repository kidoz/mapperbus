#include "core/mappers/mapper045.hpp"

#include <algorithm>
#include <charconv>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMaxPrgSize = 8UZ * 1024UZ * 1024UZ;
constexpr std::size_t kMaxChrSize = 4UZ * 1024UZ * 1024UZ;
constexpr std::size_t kMenuMaxRomSize = 512UZ * 1024UZ;

unsigned chr_and_mask(Byte control) {
    const unsigned bits = control & 0x0FU;
    return bits < 8U ? 0U : (1U << (bits - 7U)) - 1U;
}

} // namespace

Mapper045::Mapper045(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      wram_size_(header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size
                                : (header.has_battery ? 8192U : 0U)),
      persistent_wram_(header.has_battery), initial_mirror_(header.mirror_mode),
      mirror_mode_(header.mirror_mode) {
    reset();
}

Result<void> Mapper045::validate_geometry(const INesHeader& h,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (h.mapper_number != 45U)
        return std::unexpected("Mapper045 geometry validation requires mapper 45");
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 45 supports submapper 0");
    if (prg_size < 32768U || prg_size > kMaxPrgSize || prg_size % 8192U != 0U || chr_size < 8192U ||
        chr_size > kMaxChrSize || chr_size % 1024U != 0U)
        return std::unexpected(
            "Mapper 45 requires 32 KiB-8 MiB PRG and 8 KiB-4 MiB CHR ROM in bank units");
    if (h.chr_ram_size != 0U || h.chr_nvram_size != 0U ||
        (h.is_nes2 && (h.prg_ram_size > 8192U || h.prg_nvram_size > 8192U ||
                       h.prg_ram_size + h.prg_nvram_size != (h.prg_ram_size | h.prg_nvram_size) ||
                       h.prg_nvram_size != (h.has_battery ? h.prg_nvram_size : 0U))))
        return std::unexpected("Mapper 45 supports only optional 8 KiB CPU WRAM and CHR ROM");
    if (h.is_nes2 && ((h.prg_ram_size != 0U && h.prg_ram_size != 8192U) ||
                      (h.prg_nvram_size != 0U && h.prg_nvram_size != 8192U) ||
                      (h.has_battery && h.prg_nvram_size == 0U)))
        return std::unexpected("Mapper 45 native memory must match its battery-backed WRAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 45 supports ordinary H/V CIRAM");
    return {};
}

void Mapper045::reset_outer_registers() {
    outer_registers_.fill(0U);
    outer_index_ = 0U;
}

void Mapper045::power_on() {
    if (!persistent_wram_)
        wram_.fill(0U);
    reset();
}

void Mapper045::reset() {
    reset_outer_registers();
    observed_ppu_address_ = 0U;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    wram_enabled_ = true;
    wram_write_protected_ = false;
    mirror_mode_ = initial_mirror_;
}

unsigned Mapper045::mapped_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) >> 13U;
    const unsigned inner = slot == 1U                  ? bank_registers_[7U] & 0x3FU
                           : slot == 3U                ? 63U
                           : (slot == 0U) == prg_mode_ ? 62U
                                                       : bank_registers_[6U] & 0x3FU;
    const unsigned mask = 0x3FU ^ static_cast<unsigned>(outer_registers_[3U]);
    const unsigned outer =
        outer_registers_[1U] | (static_cast<unsigned>(outer_registers_[2U] & 0xC0U) << 2U);
    return (inner & mask) | outer;
}

Byte Mapper045::raw_chr_bank(Address addr) const {
    const unsigned slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    if (slot < 4U)
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    return bank_registers_[slot - 2U];
}

unsigned Mapper045::mapped_chr_bank(Address addr) const {
    const unsigned inner = raw_chr_bank(addr) & chr_and_mask(outer_registers_[2U]);
    const unsigned outer =
        outer_registers_[0U] | (static_cast<unsigned>(outer_registers_[2U] & 0xF0U) << 4U);
    return inner | outer;
}

bool Mapper045::prg_rom_disabled(Address addr) const {
    switch (menu_selection_) {
    case 1U:
        return (mapped_prg_bank(addr) & 0x80U) != 0U;
    case 2U:
        return (mapped_chr_bank(observed_ppu_address_) & 0x400U) != 0U;
    case 3U:
        return (mapped_prg_bank(addr) & 0x40U) != 0U;
    case 4U:
        return (mapped_chr_bank(observed_ppu_address_) & 0x200U) != 0U;
    default:
        return false;
    }
}

Byte Mapper045::read_prg(Address addr) {
    if (addr < 0x8000U)
        return addr >= 0x6000U && wram_enabled_ && wram_size_ != 0U ? wram_[addr & 0x1FFFU] : 0U;
    if (prg_rom_disabled(addr) || prg_rom_.empty())
        return 0U;
    const auto offset = mapped_prg_bank(addr) * 8192U + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper045::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        const auto reg = addr & 0xF001U;
        if (reg == 0x6001U) {
            reset_outer_registers();
        } else if (reg == 0x6000U && (outer_registers_[3U] & 0x40U) == 0U) {
            outer_registers_[outer_index_] =
                static_cast<Byte>(outer_index_ == 3U ? value & 0x7FU : value);
            outer_index_ = (outer_index_ + 1U) & 3U;
        }
        if (wram_enabled_ && !wram_write_protected_ && wram_size_ != 0U)
            wram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    switch ((static_cast<unsigned>(addr) >> 13U) & 3U) {
    case 0U:
        if ((addr & 1U) == 0U) {
            bank_select_ = value & 7U;
            prg_mode_ = (value & 0x40U) != 0U;
            chr_inversion_ = (value & 0x80U) != 0U;
        } else {
            bank_registers_[bank_select_] =
                bank_select_ >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        }
        break;
    case 1U:
        if ((addr & 1U) == 0U)
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        else {
            wram_enabled_ = (value & 0x80U) != 0U;
            wram_write_protected_ = (value & 0x40U) != 0U;
        }
        break;
    case 2U:
        if ((addr & 1U) == 0U)
            irq_reload_ = value;
        else {
            irq_counter_ = 0U;
            irq_reload_flag_ = true;
        }
        break;
    default:
        if ((addr & 1U) == 0U) {
            irq_enabled_ = false;
            irq_pending_ = false;
        } else {
            irq_enabled_ = true;
        }
        break;
    }
}

std::span<Byte> Mapper045::trainer_ram() {
    if (wram_size_ <= 0x1000U)
        return {};
    return std::span{wram_}.first(wram_size_).subspan(0x1000U);
}

bool Mapper045::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && wram_enabled_ && wram_size_ != 0U);
}

Byte Mapper045::prg_read_mask(Address addr) const {
    return addr >= 0x8000U && prg_rom_disabled(addr) ? 0U : 0xFFU;
}

Byte Mapper045::read_chr(Address addr) {
    if (chr_rom_.empty())
        return 0U;
    const auto offset = mapped_chr_bank(addr) * 1024U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper045::write_chr(Address, Byte) {}

MirrorMode Mapper045::mirror_mode() const {
    return mirror_mode_;
}

bool Mapper045::maps_expansion(Address addr) const {
    return addr >= 0x5000U && addr < 0x6000U;
}

Byte Mapper045::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 1U : 0xFFU;
}

Byte Mapper045::read_expansion(Address addr) {
    if (!maps_expansion(addr))
        return 0U;
    constexpr std::array<Address, 8> kDipPins = {
        0x0010U, 0x0020U, 0x0040U, 0x0080U, 0x0100U, 0x0200U, 0x0400U, 0x0800U};
    return (addr & kDipPins[menu_dip_]) != 0U ? 1U : 0U;
}

Result<void> Mapper045::configure_board_jumpers(Byte value) {
    if (value > 7U)
        return std::unexpected("Mapper 45 menu DIP switch accepts positions 0 through 7");
    menu_dip_ = value;
    return {};
}

Result<void> Mapper045::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("menu-selection-"))
        return std::unexpected("Mapper 45 profiles are menu-selection-0 through menu-selection-4");
    profile.remove_prefix(15U);
    unsigned value = 0U;
    const auto [end, error] =
        std::from_chars(profile.data(), profile.data() + profile.size(), value);
    if (error != std::errc{} || end != profile.data() + profile.size() || value > 4U)
        return std::unexpected("Mapper 45 profiles are menu-selection-0 through menu-selection-4");
    if (value != 0U && (prg_rom_.size() > kMenuMaxRomSize || chr_rom_.size() > kMenuMaxRomSize))
        return std::unexpected("Mapper 45 menu-selection wiring requires at most 512 KiB ROMs");
    menu_selection_ = static_cast<Byte>(value);
    return {};
}

bool Mapper045::observes_ppu_address() const {
    return menu_selection_ == 2U || menu_selection_ == 4U;
}

void Mapper045::observe_ppu_address(Address addr) {
    observed_ppu_address_ = addr & 0x1FFFU;
}

void Mapper045::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

std::span<const Byte> Mapper045::battery_ram() const {
    return persistent_wram_ ? std::span<const Byte>(wram_).first(wram_size_)
                            : std::span<const Byte>{};
}

void Mapper045::set_battery_ram(std::span<const Byte> data) {
    if (persistent_wram_)
        std::ranges::copy(data.first(std::min(data.size(), wram_size_)), wram_.begin());
}

uint16_t Mapper045::state_tag() const {
    return static_cast<uint16_t>(0x2D00U | menu_dip_ |
                                 (static_cast<unsigned>(menu_selection_) << 3U) |
                                 (wram_size_ != 0U ? 0x40U : 0U) | (persistent_wram_ ? 0x80U : 0U) |
                                 (initial_mirror_ == MirrorMode::Vertical ? 0x100U : 0U));
}

void Mapper045::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(static_cast<uint32_t>(chr_rom_.size()));
    if (wram_size_ != 0U)
        writer.write_array(wram_);
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((wram_enabled_ ? 0x80U : 0U) | (wram_write_protected_ ? 0x40U : 0U));
    writer.write_array(outer_registers_);
    writer.write(outer_index_);
    writer.write(observed_ppu_address_);
}

void Mapper045::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    std::array<Byte, 8192> wram{};
    if (wram_size_ != 0U)
        reader.read_array(wram);
    const auto select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    std::array<Byte, 4> outer{};
    reader.read_array(outer);
    const auto index = reader.read<Byte>();
    const auto ppu_address = reader.read<Address>();

    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() && select <= 7U && (mode & 0x3FU) == 0U &&
                       banks[6U] <= 63U && banks[7U] <= 63U &&
                       mirror <= static_cast<Byte>(MirrorMode::Vertical) && irq <= 7U &&
                       ((irq & 4U) == 0U || (irq & 2U) != 0U) && (ram & 0x3FU) == 0U &&
                       (outer[3U] & 0x80U) == 0U && index <= 3U && ppu_address <= 0x1FFFU;
    if (!valid) {
        reader.fail();
        return;
    }

    if (wram_size_ != 0U)
        wram_ = wram;
    bank_select_ = select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    wram_enabled_ = (ram & 0x80U) != 0U;
    wram_write_protected_ = (ram & 0x40U) != 0U;
    outer_registers_ = outer;
    outer_index_ = index;
    observed_ppu_address_ = ppu_address;
}

} // namespace mapperbus::core
