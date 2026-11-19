#include "core/mappers/mapper401.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMaxPrgSize = 1024UZ * 1024UZ;
constexpr std::size_t kMaxChrSize = 4UZ * 1024UZ * 1024UZ;
constexpr std::uint16_t kStateTag = 0x4010U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

unsigned chr_and_mask(Byte control) {
    const unsigned bits = control & 0x0FU;
    return bits < 8U ? 0U : (1U << (bits - 7U)) - 1U;
}

} // namespace

// Original implementation from NESdev mapper 401 oldid 18958. The header cannot
// describe the three solder choices, so explicit board jumpers select them. Reset
// behavior is unspecified; deterministic register reset and RAM retention follow
// the existing mapper-45 compatibility policy.
Mapper401::Mapper401(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      wram_size_(header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size
                                : (header.has_battery ? wram_.size() : 0U)),
      persistent_wram_(header.has_battery), initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper401::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 401U) {
        return std::unexpected("Mapper 401 requires mapper ID 401");
    }
    if (header.is_nes2 && header.submapper != 0U) {
        return std::unexpected("Mapper 401 supports submapper 0");
    }
    if (prg_size < 32768U || prg_size > kMaxPrgSize || prg_size % 8192U != 0U || chr_size < 8192U ||
        chr_size > kMaxChrSize || chr_size % 1024U != 0U) {
        return std::unexpected(
            "Mapper 401 requires 32 KiB-1 MiB PRG and 8 KiB-4 MiB CHR ROM in bank units");
    }
    if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 &&
         (header.prg_ram_size > 8192U || header.prg_nvram_size > 8192U ||
          header.prg_ram_size + header.prg_nvram_size !=
              (header.prg_ram_size | header.prg_nvram_size) ||
          header.prg_nvram_size != (header.has_battery ? header.prg_nvram_size : 0U)))) {
        return std::unexpected("Mapper 401 supports only optional 8 KiB CPU WRAM and CHR ROM");
    }
    if (header.is_nes2 && ((header.prg_ram_size != 0U && header.prg_ram_size != 8192U) ||
                           (header.prg_nvram_size != 0U && header.prg_nvram_size != 8192U) ||
                           (header.has_battery && header.prg_nvram_size == 0U))) {
        return std::unexpected("Mapper 401 native memory must match its battery-backed WRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 401 supports ordinary H/V CIRAM");
    }
    return {};
}

unsigned Mapper401::mapped_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) >> 13U;
    const unsigned inner = slot == 1U                            ? registers_.banks[7U] & 0x3FU
                           : slot == 3U                          ? 63U
                           : (slot == 0U) == registers_.prg_mode ? 62U
                                                                 : registers_.banks[6U] & 0x3FU;
    const unsigned mask = 0x3FU ^ static_cast<unsigned>(registers_.outer[3U]);
    unsigned bank = (inner & mask) | (registers_.outer[1U] & 0x1FU);
    bank |= (solder_pads_ & 1U) == 0U ? registers_.outer[2U] & 0x20U
                                      : (registers_.outer[1U] & 0x40U) >> 1U;
    bank |= (solder_pads_ & 2U) == 0U ? registers_.outer[2U] & 0x40U
                                      : (registers_.outer[1U] & 0x20U) << 1U;
    return bank;
}

Byte Mapper401::raw_chr_bank(Address addr) const {
    const unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ (registers_.chr_inversion ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

unsigned Mapper401::mapped_chr_bank(Address addr) const {
    const unsigned inner = raw_chr_bank(addr) & chr_and_mask(registers_.outer[2U]);
    const unsigned outer =
        registers_.outer[0U] | (static_cast<unsigned>(registers_.outer[2U] & 0xF0U) << 4U);
    return inner | outer;
}

bool Mapper401::prg_rom_disabled() const {
    return (solder_pads_ & 4U) != 0U && (registers_.outer[1U] & 0x80U) != 0U;
}

Byte Mapper401::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return addr >= 0x6000U && registers_.wram_enabled && wram_size_ != 0U
                   ? wram_[addr & 0x1FFFU]
                   : 0U;
    }
    if (prg_rom_disabled() || prg_rom_.empty()) {
        return 0U;
    }
    const auto offset = mapped_prg_bank(addr) * 8192U + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper401::reset_outer_registers() {
    registers_.outer.fill(0U);
    registers_.outer_index = 0U;
}

void Mapper401::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((addr & 1U) != 0U) {
            reset_outer_registers();
        } else if ((registers_.outer[3U] & 0x40U) == 0U) {
            registers_.outer[registers_.outer_index] =
                static_cast<Byte>(registers_.outer_index == 3U ? value & 0x7FU : value);
            registers_.outer_index = (registers_.outer_index + 1U) & 3U;
        }
        if (registers_.wram_enabled && !registers_.wram_write_protected && wram_size_ != 0U) {
            wram_[addr & 0x1FFFU] = value;
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch ((static_cast<unsigned>(addr) >> 13U) & 3U) {
    case 0U:
        if ((addr & 1U) == 0U) {
            registers_.bank_select = value & 7U;
            registers_.prg_mode = (value & 0x40U) != 0U;
            registers_.chr_inversion = (value & 0x80U) != 0U;
        } else {
            registers_.banks[registers_.bank_select] =
                registers_.bank_select >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        }
        break;
    case 1U:
        if ((addr & 1U) == 0U) {
            registers_.mirror = (value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        } else {
            registers_.wram_enabled = (value & 0x80U) != 0U;
            registers_.wram_write_protected = (value & 0x40U) != 0U;
        }
        break;
    case 2U:
        if ((addr & 1U) == 0U) {
            registers_.irq_reload = value;
        } else {
            registers_.irq_counter = 0U;
            registers_.irq_reload_flag = true;
        }
        break;
    default:
        if ((addr & 1U) == 0U) {
            registers_.irq_enabled = false;
            registers_.irq_pending = false;
        } else {
            registers_.irq_enabled = true;
        }
        break;
    }
}

bool Mapper401::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && registers_.wram_enabled && wram_size_ != 0U);
}

Byte Mapper401::prg_read_mask(Address addr) const {
    if (!maps_prg(addr)) {
        return 0U;
    }
    return addr >= 0x8000U && prg_rom_disabled() ? 0U : 0xFFU;
}

Byte Mapper401::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto offset = mapped_chr_bank(addr) * 1024U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper401::write_chr(Address, Byte) {}

MirrorMode Mapper401::mirror_mode() const {
    return registers_.mirror;
}

Result<void> Mapper401::configure_board_jumpers(Byte value) {
    if (value > 7U) {
        return std::unexpected("Mapper 401 solder configuration accepts values 0 through 7");
    }
    solder_pads_ = value;
    return {};
}

void Mapper401::power_on() {
    if (!persistent_wram_) {
        wram_.fill(0U);
    }
    reset();
}

void Mapper401::reset() {
    const auto mirror = initial_mirror_;
    registers_ = Registers{};
    registers_.mirror = mirror;
}

bool Mapper401::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper401::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper401::clock_irq_counter() {
    if (registers_.irq_counter == 0U || registers_.irq_reload_flag) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_reload_flag = false;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && registers_.irq_enabled) {
        registers_.irq_pending = true;
    }
}

std::span<const Byte> Mapper401::battery_ram() const {
    return persistent_wram_ ? std::span<const Byte>(wram_).first(wram_size_)
                            : std::span<const Byte>{};
}

void Mapper401::set_battery_ram(std::span<const Byte> data) {
    if (persistent_wram_) {
        std::ranges::copy(data.first(std::min(data.size(), wram_size_)), wram_.begin());
    }
}

std::uint16_t Mapper401::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | solder_pads_ | (wram_size_ != 0U ? 0x08U : 0U) |
                                      (persistent_wram_ ? 0x40U : 0U) |
                                      (initial_mirror_ == MirrorMode::Vertical ? 0x80U : 0U));
}

void Mapper401::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    if (wram_size_ != 0U) {
        writer.write_array(wram_);
    }
    writer.write(registers_.bank_select);
    writer.write<Byte>((registers_.prg_mode ? 0x40U : 0U) |
                       (registers_.chr_inversion ? 0x80U : 0U));
    writer.write_array(registers_.banks);
    writer.write(static_cast<Byte>(registers_.mirror));
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write<Byte>((registers_.irq_reload_flag ? kIrqReload : 0U) |
                       (registers_.irq_enabled ? kIrqEnabled : 0U) |
                       (registers_.irq_pending ? kIrqPending : 0U));
    writer.write<Byte>((registers_.wram_enabled ? 0x80U : 0U) |
                       (registers_.wram_write_protected ? 0x40U : 0U));
    writer.write_array(registers_.outer);
    writer.write(registers_.outer_index);
}

void Mapper401::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    std::array<Byte, 8192> wram{};
    if (wram_size_ != 0U) {
        reader.read_array(wram);
    }
    Registers next;
    reader.read(next.bank_select);
    const auto mode = reader.read<Byte>();
    reader.read_array(next.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    reader.read_array(next.outer);
    reader.read(next.outer_index);

    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() && next.bank_select <= 7U &&
                       (mode & 0x3FU) == 0U && next.banks[6U] <= 63U && next.banks[7U] <= 63U &&
                       mirror <= static_cast<Byte>(MirrorMode::Vertical) && irq <= 7U &&
                       ((irq & kIrqPending) == 0U || (irq & kIrqEnabled) != 0U) &&
                       (ram & 0x3FU) == 0U && (next.outer[3U] & 0x80U) == 0U &&
                       next.outer_index <= 3U;
    if (!valid) {
        reader.fail();
        return;
    }

    next.prg_mode = (mode & 0x40U) != 0U;
    next.chr_inversion = (mode & 0x80U) != 0U;
    next.mirror = static_cast<MirrorMode>(mirror);
    next.irq_reload_flag = (irq & kIrqReload) != 0U;
    next.irq_enabled = (irq & kIrqEnabled) != 0U;
    next.irq_pending = (irq & kIrqPending) != 0U;
    next.wram_enabled = (ram & 0x80U) != 0U;
    next.wram_write_protected = (ram & 0x40U) != 0U;
    if (wram_size_ != 0U) {
        wram_ = wram;
    }
    registers_ = next;
}

} // namespace mapperbus::core
