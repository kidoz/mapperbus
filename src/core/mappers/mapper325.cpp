#include "core/mappers/mapper325.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 8192U;
constexpr std::size_t kChrBankSize = 1024U;
constexpr std::size_t kMinPrgSize = 32768U;
constexpr std::size_t kMaxPrgSize = 512U * 1024U;
constexpr std::size_t kMinChrSize = 8192U;
constexpr std::size_t kMaxChrSize = 256U * 1024U;
constexpr std::size_t kPrgRamSize = 8192U;
constexpr std::uint16_t kStateTag = 0x3250U;

Byte swap_prg_bank_bits(Byte bank) {
    return static_cast<Byte>((bank & 0xF3U) | ((bank & 0x04U) << 1U) | ((bank & 0x08U) >> 1U));
}

Byte swap_chr_bank_bits(Byte bank) {
    return static_cast<Byte>((bank & 0xDDU) | ((bank & 0x02U) << 4U) | ((bank & 0x20U) >> 4U));
}

} // namespace

// Board wiring: NESdev mapper 325 r19372. Ordinary banking, RAM and IRQ behavior
// follows the MMC3 specification r24445; implementation code is original.
Mapper325::Mapper325(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_(header.prg_ram_size + header.prg_nvram_size, 0U), nvram_size_(header.prg_nvram_size),
      initial_mirror_(header.mirror_mode), mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper325::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 325U)
        return std::unexpected("Mapper325 requires mapper ID 325");
    if (!header.is_nes2 || header.submapper != 0U)
        return std::unexpected("Mapper 325 requires NES 2.0 submapper 0");
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 325 requires power-of-two 32-512 KiB PRG and 8-256 KiB CHR ROM");
    }
    const bool valid_ram = (header.prg_ram_size == 0U || header.prg_ram_size == kPrgRamSize) &&
                           (header.prg_nvram_size == 0U || header.prg_nvram_size == kPrgRamSize) &&
                           !(header.prg_ram_size != 0U && header.prg_nvram_size != 0U);
    if (!valid_ram || header.has_battery != (header.prg_nvram_size != 0U) ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 325 supports one optional 8 KiB PRG RAM/NVRAM chip and no CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 325 requires ordinary H/V CIRAM metadata");
    return {};
}

Byte Mapper325::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!prg_ram_enabled_ || prg_ram_.empty())
            return 0U;
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U)
        return 0U;

    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const std::size_t physical_banks = prg_rom_.size() / kPrgBankSize;
    const bool mode = (bank_select_ & 0x40U) != 0U;
    Byte bank = 0U;
    switch (slot) {
    case 0U:
        bank = mode ? static_cast<Byte>(physical_banks - 2U) : bank_registers_[6U];
        break;
    case 1U:
        bank = bank_registers_[7U];
        break;
    case 2U:
        bank = mode ? bank_registers_[6U] : static_cast<Byte>(physical_banks - 2U);
        break;
    default:
        bank = static_cast<Byte>(physical_banks - 1U);
        break;
    }
    const auto offset = static_cast<std::size_t>(swap_prg_bank_bits(bank)) * kPrgBankSize +
                        (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper325::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_ && !prg_ram_.empty())
            prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    const bool high_register =
        (addr & 0x08U) != 0U || ((addr & 0x4000U) != 0U && (addr & 0x04U) != 0U);
    switch (addr & 0xE000U) {
    case 0x8000U:
        if (!high_register) {
            bank_select_ = value & 0xC7U;
        } else {
            const unsigned index = bank_select_ & 7U;
            bank_registers_[index] = index >= 6U ? value & 0x3FU : value;
        }
        break;
    case 0xA000U:
        if (!high_register) {
            mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        } else {
            prg_ram_enabled_ = (value & 0x80U) != 0U;
            prg_ram_write_protected_ = (value & 0x40U) != 0U;
        }
        break;
    case 0xC000U:
        if (!high_register) {
            irq_latch_ = value;
        } else {
            irq_counter_ = 0U;
            irq_reload_ = true;
        }
        break;
    case 0xE000U:
        if (!high_register) {
            irq_enabled_ = false;
            irq_pending_ = false;
        } else {
            irq_enabled_ = true;
        }
        break;
    default:
        break;
    }
}

bool Mapper325::maps_prg(Address addr) const {
    if (addr >= 0x8000U)
        return true;
    return addr >= 0x6000U && addr < 0x8000U && prg_ram_enabled_ && !prg_ram_.empty();
}

Byte Mapper325::chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot = ((pattern_addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U)
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    return bank_registers_[slot - 2U];
}

Byte Mapper325::read_chr(Address addr) {
    const auto offset =
        static_cast<std::size_t>(swap_chr_bank_bits(chr_bank(addr))) * kChrBankSize +
        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper325::write_chr(Address, Byte) {}

MirrorMode Mapper325::mirror_mode() const {
    return mirror_;
}

void Mapper325::power_on() {
    reset();
    if (nvram_size_ == 0U)
        std::ranges::fill(prg_ram_, 0U);
}

void Mapper325::reset() {
    // Electrical reset values are unspecified; these are deterministic emulator policies.
    mirror_ = initial_mirror_;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
}

bool Mapper325::irq_pending() const {
    return irq_pending_;
}

void Mapper325::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper325::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_) {
        irq_counter_ = irq_latch_;
        irq_reload_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

std::uint16_t Mapper325::memory_state_tag() const {
    return static_cast<std::uint16_t>((prg_ram_.empty() ? 0U : 1U) | (nvram_size_ != 0U ? 2U : 0U) |
                                      (initial_mirror_ == MirrorMode::Vertical ? 4U : 0U));
}

void Mapper325::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_state_tag());
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(mirror_ == MirrorMode::Vertical ? 1U : 0U);
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
    writer.write_bytes(prg_ram_);
}

void Mapper325::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory_tag = reader.read<std::uint16_t>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> bank_registers{};
    reader.read_array(bank_registers);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();
    std::vector<Byte> prg_ram;
    reader.read_bytes(prg_ram, kPrgRamSize);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory_tag != memory_state_tag() ||
        (bank_select & 0x38U) != 0U || bank_registers[6U] > 0x3FU || bank_registers[7U] > 0x3FU ||
        mirror > 1U || irq_flags > 7U || ((irq_flags & 4U) != 0U && (irq_flags & 2U) == 0U) ||
        (ram_flags & 0x3FU) != 0U || prg_ram.size() != prg_ram_.size()) {
        reader.fail();
        return;
    }

    bank_select_ = bank_select;
    bank_registers_ = bank_registers;
    mirror_ = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    irq_counter_ = irq_counter;
    irq_latch_ = irq_latch;
    irq_reload_ = (irq_flags & 1U) != 0U;
    irq_enabled_ = (irq_flags & 2U) != 0U;
    irq_pending_ = (irq_flags & 4U) != 0U;
    prg_ram_enabled_ = (ram_flags & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram_flags & 0x40U) != 0U;
    prg_ram_ = std::move(prg_ram);
}

std::span<const Byte> Mapper325::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper325::set_battery_ram(std::span<const Byte> data) {
    if (data.size() != nvram_size_)
        return;
    std::ranges::copy(data, prg_ram_.begin());
}

} // namespace mapperbus::core
