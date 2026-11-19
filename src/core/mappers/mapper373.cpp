#include "core/mappers/mapper373.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x800000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x400000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kWramSize = 0x2000UZ;
constexpr std::size_t kMenuMaxRomSize = 0x080000UZ;
constexpr std::uint16_t kStateTag = 0x7400U;
constexpr Byte kGnromMode = 0x10U;

unsigned chr_and_mask(Byte control) {
    const unsigned bits = control & 0x0FU;
    return bits < 8U ? 0U : (1U << (bits - 7U)) - 1U;
}

} // namespace

// NESdev mapper 373 oldid 7982 assigns outer register 2 bit 4 to a GNROM-like
// PRG mode. That bit is not a CHR OR pin. In GNROM mode the clone sees CPU A14
// low; the selected inner bank clears bit 1 below $C000 and sets it at/above
// $C000 before the ordinary Mapper 045 inner mask and outer pins are applied.
Mapper373::Mapper373(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      wram_size_(header.prg_ram_size + header.prg_nvram_size), persistent_wram_(header.has_battery),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper373::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 373U) {
        return std::unexpected("Mapper 373 requires mapper ID 373");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 373 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 373 requires 32 KiB-8 MiB PRG ROM in 8 KiB units");
    }
    if (chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected("Mapper 373 requires 8 KiB-4 MiB CHR ROM in 1 KiB units");
    }
    const bool valid_wram =
        (header.prg_ram_size == 0U || header.prg_ram_size == kWramSize) &&
        (header.prg_nvram_size == 0U || header.prg_nvram_size == kWramSize) &&
        (header.prg_ram_size == 0U || header.prg_nvram_size == 0U) &&
        (header.has_battery ? header.prg_nvram_size == kWramSize : header.prg_nvram_size == 0U);
    if (!valid_wram || header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 373 supports CHR ROM and at most one 8 KiB CPU WRAM chip");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 373 requires ordinary H/V initial CIRAM metadata");
    }
    return {};
}

unsigned Mapper373::mapped_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) >> 13U;
    const bool swapped = (registers_.bank_mode & 0x40U) != 0U;
    const bool gnrom = (registers_.outer[2] & kGnromMode) != 0U;
    const unsigned mmc3_slot = gnrom ? slot & 1U : slot;
    unsigned inner = mmc3_slot == 1U                ? registers_.banks[7] & 0x3FU
                     : mmc3_slot == 3U              ? 63U
                     : (mmc3_slot == 0U) == swapped ? 62U
                                                    : registers_.banks[6] & 0x3FU;
    if (gnrom) {
        inner = addr < 0xC000U ? inner & ~2U : inner | 2U;
    }
    const unsigned mask = 0x3FU ^ static_cast<unsigned>(registers_.outer[3]);
    const unsigned outer =
        registers_.outer[1] | (static_cast<unsigned>(registers_.outer[2] & 0xC0U) << 2U);
    return (inner & mask) | outer;
}

Byte Mapper373::raw_chr_bank(Address addr) const {
    unsigned slot = (static_cast<unsigned>(addr) >> 10U) & 7U;
    if ((registers_.bank_mode & 0x80U) != 0U) {
        slot ^= 4U;
    }
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

unsigned Mapper373::mapped_chr_bank(Address addr) const {
    const unsigned inner = raw_chr_bank(addr) & chr_and_mask(registers_.outer[2]);
    const unsigned outer =
        registers_.outer[0] | (static_cast<unsigned>(registers_.outer[2] & 0xE0U) << 4U);
    return inner | outer;
}

bool Mapper373::prg_rom_disabled(Address addr) const {
    switch (menu_selection_) {
    case 1U:
        return (mapped_prg_bank(addr) & 0x80U) != 0U;
    case 2U:
        return (mapped_chr_bank(registers_.observed_ppu_address) & 0x400U) != 0U;
    case 3U:
        return (mapped_prg_bank(addr) & 0x40U) != 0U;
    case 4U:
        return (mapped_chr_bank(registers_.observed_ppu_address) & 0x200U) != 0U;
    default:
        return false;
    }
}

Byte Mapper373::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return (registers_.ram_flags & 0x80U) != 0U && wram_size_ != 0U ? wram_[addr & 0x1FFFU]
                                                                        : 0U;
    }
    if (addr < 0x8000U || prg_rom_disabled(addr) || prg_rom_.empty()) {
        return 0U;
    }
    const auto offset =
        static_cast<std::size_t>(mapped_prg_bank(addr)) * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper373::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((addr & 1U) != 0U) {
            reset_outer_registers();
        } else if ((registers_.outer[3] & 0x40U) == 0U) {
            auto& outer = registers_.outer[registers_.outer_index];
            outer = registers_.outer_index == 3U ? value & 0x7FU : value;
            registers_.outer_index = (registers_.outer_index + 1U) & 3U;
        }
        if ((registers_.ram_flags & 0xC0U) == 0x80U && wram_size_ != 0U) {
            wram_[addr & 0x1FFFU] = value;
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.bank_select = value & 7U;
        registers_.bank_mode = value & 0xC0U;
        break;
    case 0x8001U:
        registers_.banks[registers_.bank_select] =
            registers_.bank_select >= 6U ? value & 0x3FU : value;
        break;
    case 0xA000U:
        registers_.mirror = value & 1U;
        break;
    case 0xA001U:
        registers_.ram_flags = value & 0xC0U;
        break;
    case 0xC000U:
        registers_.irq_reload = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_flags |= 1U;
        break;
    case 0xE000U:
        registers_.irq_flags &= 1U;
        break;
    case 0xE001U:
        registers_.irq_flags |= 2U;
        break;
    default:
        break;
    }
}

bool Mapper373::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U &&
                               (registers_.ram_flags & 0x80U) != 0U && wram_size_ != 0U);
}

Byte Mapper373::prg_read_mask(Address addr) const {
    return maps_prg(addr) && (addr < 0x8000U || !prg_rom_disabled(addr)) ? 0xFFU : 0U;
}

Byte Mapper373::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto offset =
        static_cast<std::size_t>(mapped_chr_bank(addr)) * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper373::write_chr(Address, Byte) {}

MirrorMode Mapper373::mirror_mode() const {
    return registers_.mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

bool Mapper373::maps_expansion(Address addr) const {
    return addr >= 0x5000U && addr < 0x6000U;
}

Byte Mapper373::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 1U : 0xFFU;
}

Byte Mapper373::read_expansion(Address addr) {
    if (!maps_expansion(addr)) {
        return 0U;
    }
    constexpr std::array<Address, 8> kDipPins = {
        0x0010U, 0x0020U, 0x0040U, 0x0080U, 0x0100U, 0x0200U, 0x0400U, 0x0800U};
    return (addr & kDipPins[menu_dip_]) != 0U ? 1U : 0U;
}

Result<void> Mapper373::configure_board_jumpers(Byte value) {
    if (value > 7U) {
        return std::unexpected("Mapper 373 menu DIP switch accepts positions 0 through 7");
    }
    menu_dip_ = value;
    return {};
}

Result<void> Mapper373::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("menu-selection-")) {
        return std::unexpected("Mapper 373 profiles are menu-selection-0 through menu-selection-4");
    }
    profile.remove_prefix(15U);
    unsigned value = 0U;
    const auto [end, error] =
        std::from_chars(profile.data(), profile.data() + profile.size(), value);
    if (error != std::errc{} || end != profile.data() + profile.size() || value > 4U) {
        return std::unexpected("Mapper 373 profiles are menu-selection-0 through menu-selection-4");
    }
    if (value != 0U && (prg_rom_.size() > kMenuMaxRomSize || chr_rom_.size() > kMenuMaxRomSize)) {
        return std::unexpected("Mapper 373 menu-selection wiring requires at most 512 KiB ROMs");
    }
    menu_selection_ = static_cast<Byte>(value);
    return {};
}

bool Mapper373::observes_ppu_address() const {
    return menu_selection_ == 2U || menu_selection_ == 4U;
}

void Mapper373::observe_ppu_address(Address addr) {
    registers_.observed_ppu_address = addr & 0x1FFFU;
}

void Mapper373::reset_outer_registers() {
    registers_.outer.fill(0U);
    registers_.outer_index = 0U;
}

void Mapper373::power_on() {
    reset();
    if (!persistent_wram_) {
        wram_.fill(0U);
    }
}

void Mapper373::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_ == MirrorMode::Vertical ? 1U : 0U;
}

bool Mapper373::irq_pending() const {
    return (registers_.irq_flags & 4U) != 0U;
}

void Mapper373::acknowledge_irq() {
    registers_.irq_flags &= 3U;
}

void Mapper373::clock_irq_counter() {
    if (registers_.irq_counter == 0U || (registers_.irq_flags & 1U) != 0U) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_flags &= 6U;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & 2U) != 0U) {
        registers_.irq_flags |= 4U;
    }
}

std::span<const Byte> Mapper373::battery_ram() const {
    return persistent_wram_ ? std::span<const Byte>(wram_).first(wram_size_)
                            : std::span<const Byte>{};
}

void Mapper373::set_battery_ram(std::span<const Byte> data) {
    if (persistent_wram_) {
        std::ranges::copy(data.first(std::min(data.size(), wram_size_)), wram_.begin());
    }
}

std::uint16_t Mapper373::state_tag() const {
    return static_cast<std::uint16_t>(
        kStateTag | menu_dip_ | (static_cast<unsigned>(menu_selection_) << 3U) |
        (wram_size_ != 0U ? 0x40U : 0U) | (persistent_wram_ ? 0x80U : 0U) |
        (initial_mirror_ == MirrorMode::Vertical ? 0x100U : 0U));
}

void Mapper373::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    if (wram_size_ != 0U) {
        writer.write_array(wram_);
    }
    writer.write(registers_.bank_select);
    writer.write(registers_.bank_mode);
    writer.write_array(registers_.banks);
    writer.write(registers_.mirror);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write(registers_.irq_flags);
    writer.write(registers_.ram_flags);
    writer.write_array(registers_.outer);
    writer.write(registers_.outer_index);
    writer.write(registers_.observed_ppu_address);
}

void Mapper373::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    std::array<Byte, kWramSize> wram{};
    if (wram_size_ != 0U) {
        reader.read_array(wram);
    }
    Registers next;
    reader.read(next.bank_select);
    reader.read(next.bank_mode);
    reader.read_array(next.banks);
    reader.read(next.mirror);
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    reader.read(next.irq_flags);
    reader.read(next.ram_flags);
    reader.read_array(next.outer);
    reader.read(next.outer_index);
    reader.read(next.observed_ppu_address);

    const bool valid_registers = next.bank_select <= 7U && (next.bank_mode & 0x3FU) == 0U &&
                                 next.banks[6] <= 0x3FU && next.banks[7] <= 0x3FU &&
                                 next.mirror <= 1U && next.irq_flags <= 7U &&
                                 ((next.irq_flags & 4U) == 0U || (next.irq_flags & 2U) != 0U) &&
                                 (next.ram_flags & 0x3FU) == 0U && (next.outer[3] & 0x80U) == 0U &&
                                 next.outer_index <= 3U && next.observed_ppu_address <= 0x1FFFU;
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || !valid_registers) {
        reader.fail();
        return;
    }

    if (wram_size_ != 0U) {
        wram_ = wram;
    }
    registers_ = next;
}

} // namespace mapperbus::core
